// Copyright (c) 2023. ByteDance Inc. All rights reserved.


#pragma once

#include <cstdint>
#include <chrono>
#include "utils/thirdparty/quiche/rtt_stats.h"
#include "basefw/base/log.h"
#include "utils/rttstats.h"
#include "utils/transporttime.h"
#include "utils/defaultclock.hpp"
#include "sessionstreamcontroller.hpp"
#include "packettype.h"

enum class CongestionCtlType : uint8_t
{
    none = 0,
    reno = 1,
    ours =2
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
           << "losttic: " << losttic.ToDebuggingValue() << " ";
        return ss.str();
    }
};

struct AckEvent
{
    /** since we receive packets one by one, each packet carries only one data piece*/
    bool valid{ false };
    InflightPacket ackPacket;
    Timepoint sendtic{ Timepoint::Infinite() };
    Timepoint losttic{ Timepoint::Infinite() };
    Timepoint recvtic{ Timepoint::Infinite() };

    std::string DebugInfo() const
    {
        std::stringstream ss;
        ss << "valid: " << valid << " "
           << "ackpkt:{"
           << "seq: " << ackPacket.seq << " "
           << "dataid: " << ackPacket.pieceId << " "
           << "} "
           << "sendtic: " << sendtic.ToDebuggingValue() << " "
           << "losttic: " << losttic.ToDebuggingValue() << " "
           << "recvtic: " << recvtic.ToDebuggingValue() << " ";
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
        SPDLOG_TRACE("inflight: {} eventtime: {} ackEvent:{} ", downloadingmap.DebugInfo(),
                eventtime.ToDebuggingValue(), ackEvent.DebugInfo());
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
            losses.losttic = eventtime;
            losses.valid = true;
            SPDLOG_DEBUG("losses: {}", losses.DebugInfo());
        }
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

    /////
    virtual uint32_t GetCWND() = 0;

    virtual uint32_t GetSendNum() = 0;

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

class RenoCongestionControl : public CongestionCtlAlgo
{
public:

    explicit RenoCongestionControl(const RenoCongestionCtlConfig& ccConfig)
    {
        m_ssThresh = ccConfig.ssThresh;
        m_minCwnd = ccConfig.minCwnd;
        m_maxCwnd = ccConfig.maxCwnd;
        SPDLOG_DEBUG("m_ssThresh:{}, m_minCwnd:{}, m_maxCwnd:{} ", m_ssThresh, m_minCwnd, m_maxCwnd);
    }

    ~RenoCongestionControl() override
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
            SPDLOG_DEBUG("[custom] lostpkt: {}, cwnd: {}", lostpkt.pieceId, pktWndMap[lostpkt.pieceId]);
        }

        float loss_rate = 0.01;
        if (lossEvent.lossPackets.size() < std::max(int(m_cwnd* loss_rate), 3)) {
            //m_maxCwnd = maxWnd - lossEvent.lossPackets.size();
            return;
        }

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
    uint32_t m_maxCwnd{ 128 };
    uint32_t m_ssThresh{ 32 };/** slow start threshold*/

    std::map<DataNumber, uint32_t> pktWndMap;
};

struct OursCongestionCtlConfig {
    uint32_t period;
    double peak_gain;
};

class OursCongestionControl : public CongestionCtlAlgo {
public:

    explicit OursCongestionControl(const OursCongestionCtlConfig &ccConfig)
        : RTprop(Duration::Infinite()), nextPeriodTime(Timepoint::Zero()) {
        period = ccConfig.period;
        peak_gain = ccConfig.peak_gain;

        delivered = 0;
        receivedSeq = 0;
        cwnd_gain = 1.0;
        period_cnt = 0;
        isStartUp = true;
        isDrain = false;
        isWait = false;
        last_delivered = 0;
        inflight = 0;
    }

    CongestionCtlType GetCCtype() override {
        return CongestionCtlType::ours;
    }

    void OnDataSent(InflightPacket& sentpkt) override {
        sentpkt.delivered = delivered;
        inflight++;
        sendW = std::max(sendW-1, uint32_t(0));
        if (isDrain)
        {
            ticNum--;
            if (ticNum == 0) 
            {
                isDrain = false;
                cwnd_gain = 1.0;
                ticNum = GetCWND();
                SPDLOG_DEBUG("Drain over.");
            }   
        }
        if (!isStartUp && !isDrain)
        {
            ticNum--;
            if (ticNum == 0)
            {
                if (cwnd_gain == 1.0)
                {
                    cwnd_gain = 1.0;
                    ticNum = GetCWND();
                    cwnd_gain = 1 + peak_gain;
                    SPDLOG_DEBUG("begin to shake (up).");
                }
                else if (cwnd_gain == 1 + peak_gain)
                {
                    //cwnd_gain = 1.0;
                    ticNum = 1;
                    cwnd_gain = 1.0;
                    SPDLOG_DEBUG("begin to shake (down).");
                }
                // else if (cwnd_gain == 1 - peak_gain)
                // {
                //     cwnd_gain = 1.0;
                //     ticNum = GetCWND();
                //     SPDLOG_DEBUG("begin to steady.");
                // }
            }
        }
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

    uint32_t GetSendNum() override {
        return sendW;
    }

    uint32_t GetCWND() override {
        double detectBw = btlBw;
        DataNumber bdp = 8;
        if (!RTprop.IsInfinite()) {
            bdp = std::max(DataNumber(RTprop.ToMilliseconds() * detectBw), 1);
        }
        SPDLOG_DEBUG("btlBw: {}, cwnd_gain: {}, bdp: {}", btlBw, cwnd_gain, bdp);

        return bdp+std::min(recvW, bdp/4U);
    }

private:
    void OnDataRecv(const AckEvent& ackEvent, RttStats& rttstats) {
        RTprop = std::min(rttstats.latest_rtt(), RTprop);
        
        receivedSeq = ackEvent.ackPacket.seq;
        delivered++;
        inflight--;
        

        if (lastReceivedTime != Timepoint::Zero() && lastPktSendTime != Timepoint::Zero())
        {
            Duration receivedDur = ackEvent.recvtic - lastReceivedTime;
            Duration sendDur = ackEvent.sendtic - lastPktSendTime;
            double nowRate = 0;

            if (sendDur.ToMicroseconds() <= 200)
            {
                avgRecDur = alpha * receivedDur + (1-alpha) * avgRecDur;
                nowRate = double(1)*1000 / avgRecDur.ToMicroseconds();
                btlBw = nowRate;
            }

            if (Clock::GetClock()->Now() >= nextPeriodTime && ackEvent.ackPacket.groupId != lastGroupId && rttstats.smoothed_rtt() >= RTprop + 2*Duration::FromMicroseconds(int(1000/btlBw))) {
                btlBw *= 0.9;
                nextPeriodTime = Clock::GetClock()->Now() + RTprop;
                
                SPDLOG_DEBUG("Rtt is too long");
            }
            //deliveryRate = alpha * nowRate + (1-alpha) * deliveryRate;
            lastGroupId = ackEvent.ackPacket.groupId;
            SPDLOG_DEBUG("receivedDur: {}, sendDur: {}, nowRate: {}", receivedDur.ToMicroseconds(), sendDur.ToMicroseconds(), nowRate);
        }
        lastReceivedTime = ackEvent.recvtic;
        lastPktSendTime = ackEvent.sendtic;

        last_delivered = ackEvent.ackPacket.delivered;

        uint32_t nowCWND = GetCWND();
        if (nowCWND > inflight+recvNum) recvNum++;

        if (sendW != 0) {
            recvNum = 0;
            SPDLOG_DEBUG("Send pkt too slow");
        }

        if (isStartUp){
            if (recvNum != 0 && recvNum % recvW == 0) {
                sendW = std::min(int(recvW*2), 8);
                lastSentW = sendW;
                if (recvNum + recvW > period && recvW <= period) {
                    recvW += 1;
                    recvNum = 0;
                }
                if (recvW > period) {
                    recvW = period;
                    if (inflight >= nowCWND - sendW){
                        isStartUp = false;
                        SPDLOG_DEBUG("Start up over");
                    }
                }
            }
        } else {
            if (recvNum == recvW) {
                recvNum = 0;
                if (nowCWND - inflight < std::min(recvW, nowCWND/4)) {
                    sendW = 0;
                    recvW = std::min(4U, nowCWND/4);
                    recvNum = nowCWND-inflight;
                } else {
                    sendW = std::min(std::min(2*recvW, 8U), nowCWND - inflight);
                    recvW = std::min(recvW, nowCWND/2);
                }
            } 
            if (sendW != 0) lastSentW = sendW;
        }


        // if (!isStartUp && recvNum == 0) {
            
        //     double incRate = std::max(double(inflight+recvW) / nowCWND, 0.5);
            
        //     if (nowCWND - inflight < std::min(recvW, nowCWND/2)){
        //         recvNum = std::min(recvW+1, nowCWND/2) - (nowCWND - inflight);
        //         recvW = std::min(std::min(recvW+1, nowCWND/2), 8U);
        //         sendW = 0;
        //     } else {
        //         sendW = std::min(2*recvW, 8U);
        //         recvW = std::min(recvW, nowCWND/2);
        //     }
        //     // if (lastSentW == 8 || lastSentW >= GetCWND()/4) {
        //     //     recvW = std::max(uint32_t(std::floor(lastSentW*incRate)), uint32_t(1));
        //     //     sendW = lastSentW;
        //     //     SPDLOG_DEBUG("lastSentW: {}", lastSentW);
        //     // } else if (incRate < 1){
        //     //     sendW = std::min(uint32_t(std::ceil(recvW/incRate)), uint32_t(8));
        //     //     recvW = std::min(recvW+1, sendW);
        //     //     SPDLOG_DEBUG("incRate < 1");
        //     // } else {
        //     //     recvW++;
        //     //     sendW = recvW;
        //     //     SPDLOG_DEBUG("incRate equals 1");
        //     // }
            
            
        //     if (sendW != 0) lastSentW = sendW;
        //     if (sendW > nowCWND-inflight) {
        //         recvW = sendW-(nowCWND-inflight);
        //         sendW = 0;
        //     }     
        // }

        if (sendW==1) SPDLOG_DEBUG("sendW is only 1");

       

        if (Clock::GetClock()->Now() >= nextPeriodTime) 
        {
            // if (!RTprop.IsInfinite()) {
            //     nextPeriodTime = Clock::GetClock()->Now() + RTprop;
            // }
            
            
            oldBw = btlBw;
        }

        SPDLOG_DEBUG("sendW: {}, recvNum: {}, recvW: {}", sendW, recvNum, recvW);
        SPDLOG_DEBUG("delivered: {}, pkt_delivered: {}", delivered, ackEvent.ackPacket.delivered);
        SPDLOG_DEBUG("inflight: {}, lastSentW: {}", inflight, lastSentW);
        SPDLOG_DEBUG("period_cnt: {}, cwnd_gain: {}", period_cnt, cwnd_gain);
        SPDLOG_DEBUG("RTprop: {}, btlBw: {}, oldBw: {}", RTprop.ToDebuggingValue(), btlBw, oldBw);
    }

    void OnDataLoss(const LossEvent& lossEvent)
    {
        inflight -= lossEvent.lossPackets.size();
        uint32_t nowCWND = GetCWND();
        if (inflight < nowCWND) {
            recvNum = 0;
            sendW = std::min(std::min(nowCWND - inflight, uint32_t(lossEvent.lossPackets.size())), 8U);
            lastSentW = sendW;
        }
        
        //ticNum = 1;
        //cwnd_gain = 1.0;
        SPDLOG_DEBUG("sendW: {}, recvNum: {}, recvW: {}", sendW, recvNum, recvW);
        SPDLOG_DEBUG("after Loss, inflight={}", inflight);
    }

    Duration RTprop;
    DataNumber delivered;
    DataNumber receivedSeq;
    double btlBw{0.0001};
    double oldBw;
    double cwnd_gain;
    uint32_t last_delivered;
    bool isStartUp;
    bool isDrain;
    bool isWait;
    uint32_t inflight;
    Duration avgRecDur{ Duration::FromMicroseconds(10000) };
    double alpha{ 0.1 };
    uint32_t ticNum{ 0 };
    uint32_t sendW{ 1 };
    uint32_t recvW{ 1 };
    uint32_t recvNum{ 0 };
    uint32_t lastSentW{ 1 };

    uint32_t period;
    uint32_t period_cnt;
    double peak_gain;
    Timepoint nextPeriodTime;
    Timepoint lastReceivedTime{ Timepoint::Zero() };
    Timepoint lastPktSendTime{ Timepoint::Zero() };
    uint32_t lastGroupId{ 0 };
};