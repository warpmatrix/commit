// Copyright (c) 2023. ByteDance Inc. All rights reserved.


#pragma once

#include <cstdint>
#include <chrono>
#include <cmath>
#include "basefw/base/log.h"
#include "utils/thirdparty/quiche/rtt_stats.h"
#include "utils/rttstats.h"
#include "utils/transporttime.h"
#include "utils/defaultclock.hpp"
#include "sessionstreamcontroller.hpp"
#include "packettype.h"
#include "demo/utils/thirdparty/quiche/quic_time.h"


enum class CongestionCtlType : uint8_t {
    none = 0,
    reno,
    bbr
};

struct LossEvent
{
    bool valid{ false };
    // There may be multiple timeout events at one time
    std::vector<InflightPacket> lossPackets;
    Timepoint losttic{ Timepoint::Infinite() };

    std::string DebugInfo() const
    {
        std::stringstream ss;
        ss << "valid: " << valid << " "
           << "lossPackets:{";
        for (const auto& pkt: lossPackets)
        {
            ss << pkt;
        }

        ss << "} "
           << "losttic: " << losttic.ToDebuggingValue();
        return ss.str();
    }
};

struct AckEvent
{
    /** since we receive packets one by one, each packet carries only one data piece*/
    bool valid{ false };
    InflightPacket ackPacket;
    Timepoint sendtic{ Timepoint::Infinite() };

    bool isLastInGroup{ false };

    std::string DebugInfo() const
    {
        std::stringstream ss;
        ss << "valid: " << valid << " "
           << "ackpkt:{" << ackPacket << "} "
           << "sendtic: " << sendtic.ToDebuggingValue() << " ";
        return ss.str();
    }
};

/** This is a loss detection algorithm interface
 *  similar to the GeneralLossAlgorithm interface in Quiche project
 * */
class LossDetectionAlgo
{

public:
    /** @brief this function will be called when loss detection may happen, like timer alarmed or packet acked
     * input
     * @param downloadingmap all the packets inflight, sent but not acked or lost
     * @param eventtime timpoint that this function is called
     * @param ackEvent  ack event that trigger this function, if any
     * @param maxacked max sequence number that has acked
     * @param rttStats RTT statics module
     * output
     * @param losses loss event
     * */
    virtual void DetectLoss(const InFlightPacketMap& downloadingmap, Timepoint eventtime,
            const AckEvent& ackEvent, uint64_t maxacked, LossEvent& losses, RttStats& rttStats)
    {
    };

    virtual ~LossDetectionAlgo() = default;

};

class DefaultLossDetectionAlgo : public LossDetectionAlgo
{/// Check loss event based on RTO
public:
    void DetectLoss(const InFlightPacketMap& downloadingmap, Timepoint eventtime, const AckEvent& ackEvent,
            uint64_t maxacked, LossEvent& losses, RttStats& rttStats) override
    {
        /** RFC 9002 Section 6
         * */
        Duration maxrtt = std::max(rttStats.previous_srtt(), rttStats.latest_rtt());
        if (maxrtt == Duration::Zero())
        {
            SPDLOG_DEBUG(" {}", maxrtt == Duration::Zero());
            maxrtt = rttStats.SmoothedOrInitialRtt();
        }
        Duration loss_delay = maxrtt + (maxrtt * (5.0 / 4.0));
        loss_delay = std::max(loss_delay, Duration::FromMicroseconds(1));
        SPDLOG_TRACE(" maxrtt: {}, loss_delay: {}", maxrtt.ToDebuggingValue(), loss_delay.ToDebuggingValue());
        for (const auto& pkt_itor: downloadingmap.inflightPktMap)
        {
            const auto& pkt = pkt_itor.second;
            if (Timepoint(pkt.sendtic + loss_delay) <= eventtime)
            {
                losses.lossPackets.emplace_back(pkt);
            }
        }
        if (!losses.lossPackets.empty())
        {
            losses.valid = true;
        }
        // losses.losttic = eventtime - loss_delay;
        losses.losttic = eventtime;
    }

    ~DefaultLossDetectionAlgo() override
    {
    }

private:
};


class CongestionCtlAlgo
{
public:

    virtual ~CongestionCtlAlgo() = default;

    virtual CongestionCtlType GetCCtype() = 0;

    /////  Event
    virtual void OnDataSent(InflightPacket& sentpkt) = 0;

    virtual void OnDataAckOrLoss(const AckEvent& ackEvent, const LossEvent& lossEvent, RttStats& rttstats) = 0;

    virtual void SetWait(bool sig) = 0;

    virtual uint32_t GetBatchSize() = 0;

    /////
    virtual uint32_t GetCWND() = 0;

//    virtual uint32_t GetFreeCWND() = 0;

};

/// config or setting for specific cc algo
/// used for pass parameters to CongestionCtlAlgo
struct RenoCongestionCtlConfig
{
    uint32_t minCwnd{ 1 };
    uint32_t maxCwnd{ 64 };
    uint32_t ssThresh{ 32 };/** slow start threshold*/
};

class RenoCongestionContrl : public CongestionCtlAlgo
{
public:

    explicit RenoCongestionContrl(const RenoCongestionCtlConfig& ccConfig)
    {
        m_ssThresh = ccConfig.ssThresh;
        m_minCwnd = ccConfig.minCwnd;
        m_maxCwnd = ccConfig.maxCwnd;
        SPDLOG_DEBUG("m_ssThresh:{}, m_minCwnd:{}, m_maxCwnd:{} ", m_ssThresh, m_minCwnd, m_maxCwnd);
    }

    ~RenoCongestionContrl() override
    {
        SPDLOG_DEBUG("");
    }

    CongestionCtlType GetCCtype() override
    {
        return CongestionCtlType::reno;
    }

    void OnDataSent(InflightPacket& sentpkt) override
    {
        SPDLOG_TRACE("");
        pktWndMap[sentpkt.pieceId] = GetCWND();
    }

    void OnDataAckOrLoss(const AckEvent& ackEvent, const LossEvent& lossEvent, RttStats& rttstats) override
    {
        SPDLOG_TRACE("ackevent:{}, lossevent:{}", ackEvent.DebugInfo(), lossEvent.DebugInfo());
        if (lossEvent.valid)
        {
            OnDataLoss(lossEvent);
        }

        if (ackEvent.valid)
        {
            OnDataRecv(ackEvent);
        }

    }

    /////
    uint32_t GetCWND() override
    {
        SPDLOG_TRACE(" {}", m_cwnd);
        return m_cwnd;
    }

//    virtual uint32_t GetFreeCWND() = 0;

private:

    bool InSlowStart()
    {
        bool rt = false;
        if (m_cwnd < m_ssThresh)
        {
            rt = true;
        }
        else
        {
            rt = false;
        }
        SPDLOG_TRACE(" m_cwnd:{}, m_ssThresh:{}, InSlowStart:{}", m_cwnd, m_ssThresh, rt);
        return rt;
    }

    bool LostCheckRecovery(Timepoint largestLostSentTic)
    {
        SPDLOG_DEBUG("largestLostSentTic:{},lastLagestLossPktSentTic:{}",
                largestLostSentTic.ToDebuggingValue(), lastLagestLossPktSentTic.ToDebuggingValue());
        /** If the largest sent tic of this loss event,is bigger than the last sent tic of the last lost pkt
         * (plus a 10ms correction), this session is in Recovery phase.
         * */
        if (lastLagestLossPktSentTic.IsInitialized() &&
            (largestLostSentTic + Duration::FromMilliseconds(10) > lastLagestLossPktSentTic))
        {
            SPDLOG_DEBUG("In Recovery");
            return true;
        }
        else
        {
            // a new timelost
            lastLagestLossPktSentTic = largestLostSentTic;
            SPDLOG_DEBUG("new loss");
            return false;
        }

    }

    void ExitSlowStart()
    {
        SPDLOG_DEBUG("m_ssThresh:{}, m_cwnd:{}", m_ssThresh, m_cwnd);
        m_ssThresh = m_cwnd;
    }


    void OnDataRecv(const AckEvent& ackEvent)
    {
        SPDLOG_DEBUG("ackevent:{},m_cwnd:{}", ackEvent.DebugInfo(), m_cwnd);
        if (InSlowStart())
        {
            /// add 1 for each ack event
            m_cwnd += 1;

            if (m_cwnd >= m_ssThresh)
            {
                ExitSlowStart();
            }
            SPDLOG_DEBUG("new m_cwnd:{}", m_cwnd);
        }
        else
        {
            /// add cwnd for each RTT
            m_cwndCnt++;
            m_cwnd += m_cwndCnt / m_cwnd;
            if (m_cwndCnt == m_cwnd)
            {
                m_cwndCnt = 0;
            }
            SPDLOG_DEBUG("not in slow start state,new m_cwndCnt:{} new m_cwnd:{}",
                    m_cwndCnt, ackEvent.DebugInfo(), m_cwnd);

        }
        m_cwnd = BoundCwnd(m_cwnd);

        SPDLOG_DEBUG("after RX, m_cwnd={}", m_cwnd);
    }

    void OnDataLoss(const LossEvent& lossEvent)
    {
        SPDLOG_DEBUG("lossevent:{}", lossEvent.DebugInfo());
        Timepoint maxsentTic{ Timepoint::Zero() };

        uint32_t maxWnd = 0;
        for (const auto& lostpkt: lossEvent.lossPackets)
        {
            maxsentTic = std::max(maxsentTic, lostpkt.sendtic);
            maxWnd = std::max(maxWnd, pktWndMap[lostpkt.pieceId]);
            SPDLOG_DEBUG("[custom] lostpkt: {}, cwnd: {}",
                lostpkt.pieceId, pktWndMap[lostpkt.pieceId]
            );
        }
        // float loss_rate = 0.02;
        // if (lossEvent.lossPackets.size() > std::max(int(m_cwnd * loss_rate), 3)) {
        //     m_maxCwnd = maxWnd - lossEvent.lossPackets.size();
        // }

        /** In Recovery phase, cwnd will decrease 1 pkt for each lost pkt
         *  Otherwise, cwnd will cut half.
         * */
        if (InSlowStart())
        {
            // loss in slow start, just cut half
            m_cwnd = m_cwnd / 2;
            m_cwnd = BoundCwnd(m_cwnd);

        }
        else //if (!LostCheckRecovery(maxsentTic))
        {
            // Not In slow start and not inside Recovery state
            // Cut half
            m_cwnd = m_cwnd / 2;
            m_cwnd = BoundCwnd(m_cwnd);
            m_ssThresh = m_cwnd;
            // enter Recovery state
        }
        SPDLOG_DEBUG("after Loss, m_cwnd={}", m_cwnd);
    }


    uint32_t BoundCwnd(uint32_t trySetCwnd)
    {
        return std::max(m_minCwnd, std::min(trySetCwnd, m_maxCwnd));
    }

    uint32_t m_cwnd{ 1 };
    uint32_t m_cwndCnt{ 0 }; /** in congestion avoid phase, used for counting ack packets*/
    Timepoint lastLagestLossPktSentTic{ Timepoint::Zero() };


    uint32_t m_minCwnd{ 1 };
    uint32_t m_maxCwnd{ 64 };
    uint32_t m_ssThresh{ 32 };/** slow start threshold*/

    std::map<DataNumber, uint32_t> pktWndMap;
};

struct BBRCongestionCtlConfig {
    uint32_t period;
    double peak_gain;
};

class BBRCongestionControl : public CongestionCtlAlgo {
public:

    explicit BBRCongestionControl(const BBRCongestionCtlConfig &ccConfig)
        : RTprop(Duration::Infinite()), nextPeriodTime(Timepoint::Zero()) {
        period = ccConfig.period;
        peak_gain = ccConfig.peak_gain;

        delivered = 0;
        receivedSeq = 0;
        cwnd_gain = 2.0;
        period_cnt = 0;
        isStartUp = true;
        isDrain = false;
        isWait = false;
        last_delivered = 0;
        inflight = 0;
    }

    CongestionCtlType GetCCtype() override {
        return CongestionCtlType::bbr;
    }

    void SetWait(bool sig) override {
        isWait = sig;
    }

    void OnDataSent(InflightPacket& sentpkt) override {
        sentpkt.delivered = delivered;
        sentpkt.receivedSeq = receivedSeq;
        sentpkt.needWait = isWait;
        inflight++;
    }

    void OnDataAckOrLoss(const AckEvent& ackEvent, const LossEvent& lossEvent, RttStats& rttstats) override {
        SPDLOG_TRACE("ackevent:{}, lossevent:{}", ackEvent.DebugInfo(), lossEvent.DebugInfo());
        if (lossEvent.valid) {
            OnDataLoss(lossEvent);
        }
        if (ackEvent.valid) {
            OnDataRecv(ackEvent, rttstats);
        }
    }

    uint32_t GetBatchSize() override {
        double detectBw = cwnd_gain * btlBw;
        DataNumber bdp = 10;
        if (!RTprop.IsInfinite()) {
            bdp = std::max(DataNumber(2 * intervalSetting * detectBw), 1);
        }
        SPDLOG_DEBUG("btlBw: {}, cwnd_gain: {}, batch size: {}", btlBw, cwnd_gain, bdp);

        return bdp;
    }

    uint32_t GetCWND() override {
        double detectBw = cwnd_gain * btlBw;
        DataNumber bdp = 10;
        if (!RTprop.IsInfinite()) {
            bdp = std::max(DataNumber(RTprop.ToMilliseconds() * detectBw), 1);
        }
        SPDLOG_DEBUG("btlBw: {}, cwnd_gain: {}, bdp: {}", btlBw, cwnd_gain, bdp);

        return bdp;
    }

private:
    void OnDataRecv(const AckEvent& ackEvent, RttStats& rttstats) {
        RTprop = std::min(rttstats.latest_rtt(), RTprop);
        receivedSeq = ackEvent.ackPacket.seq;
        delivered++;
        inflight--;
        last_delivered = ackEvent.ackPacket.delivered;
        int dataDelivered = delivered - last_delivered;
    
        double deliveryRate = double(dataDelivered) / rttstats.latest_rtt().ToMilliseconds();       
        double oldBw = btlBw;
        btlBw = std::max(deliveryRate, btlBw);
        
        if (isStartUp){
            cwnd_gain = 2.0;
        } else {
            cwnd_gain = 1.25;
        }

        if (Clock::GetClock()->Now() >= nextPeriodTime) {
            //SPDLOG_DEBUG("Try change cwnd.");
            if (!RTprop.IsInfinite()) {
                nextPeriodTime = Clock::GetClock()->Now() + rttstats.smoothed_rtt();
            }
            if (isStartUp && oldBw >= deliveryRate) {
                period_cnt++;
                if (period_cnt == period) {
                    isStartUp = false;
                    period_cnt = 0;
                    SPDLOG_DEBUG("start up over");
                }
            } else if (!isStartUp && oldBw < deliveryRate) {
                period_cnt++;
                if (period_cnt == period) {
                    isStartUp = true;
                    period_cnt = 0;
                    SPDLOG_DEBUG("start up retry");
                }
            } else {
                period_cnt = 0;
                SPDLOG_DEBUG("keep steady");
            }
        } 
            
        SPDLOG_DEBUG("delivered: {}, pkt_delivered: {}", delivered, ackEvent.ackPacket.delivered);
        SPDLOG_DEBUG("period_cnt: {} deliveryRate: {}, cwnd_gain: {}", period_cnt, deliveryRate, cwnd_gain);
        SPDLOG_DEBUG("RTprop: {}, btlBw: {}, oldBw: {}", RTprop.ToDebuggingValue(), btlBw, oldBw);
    }

    void OnDataLoss(const LossEvent& lossEvent)
    {
        SPDLOG_DEBUG("lossevent: {}", lossEvent.DebugInfo());
        Timepoint maxsentTic{ Timepoint::Zero() };

        if (lossEvent.lossPackets.size() > 3) {
            btlBw = 0.5*btlBw;
        } else {
            btlBw = 0.9*btlBw;
        }

    }

    Duration RTprop;
    DataNumber delivered;
    DataNumber receivedSeq;
    double btlBw;
    double cwnd_gain;
    int last_delivered;
    bool isStartUp;
    bool isDrain;
    bool isWait;
    int inflight;
    uint32_t intervalSetting{ 20 };

    uint32_t period;
    uint32_t period_cnt;
    double peak_gain;
    Timepoint nextPeriodTime;
};
