Please refer to readme.pdf for details. 
 
主要需要实现的组件（在MPDtest.cpp中完成注册）：

- DemoTransportCtlConfig：定义传输控制相关参数
- DemoTransportCtl：通过回调函数控制传输行为
- DemoTransportCtlFactory：在合适的时间创建 controller

整体的执行流程：

1. `MPD::CreatePlayer`
2. `MPD::InitPlayer`: info player events by `PlayerEventHandler`
3. `MPD::SetTransportModuleSettings` (register transport module setting)
4. `MPD::StartPlayer`
    1. `DemoTransportCtl`
    2. `DemoTransportCtl::StartTransportController`
        1. `RRMultiPathScheduler`
        2. `RRMultiPathScheduler::StartMultiPathScheduler`
    3. add task: client (connect, add session): `DemoTransportCtl::OnSessionCreate`
        1. `SessionStreamController`
        2. `SessionStreamController::StartSessionStreamCtl`
            1. RenoCongestionContrl::RenoCongestionContrl
            2. PacketSender
            3. DefaultLossDetectionAlgo
        3. `RRMultiPathScheduler::OnSessionCreate`
5. `MPD::StartDownload`
    1. `RRMultiPathScheduler::StartMultiPathScheduler`
    2. start download task
        1. `MPDTransportController::OnPieceTaskAdding`
        2. `RRMultiPathScheduler::DoMultiPathSchedule`
        1. `RRMultiPathScheduler::SortSession`
        2. `RRMultiPathScheduler::FillUpSessionTask`
            1. put lost packets back into main download queue
            2. try to request enough piece cnt from up layer, if necessary
            3. fill up each session Queue, based on min RTT first order, and send
    3. `MPDTransportController::OnDataSent`: `SessionStreamController::OnDataRequestPktSent`
        1. `InFlightPacketMap::AddSentPacket`
        2. `RenoCongestionContrl::OnDataSent`
    4. `SessionStreamController::OnLossDetectionAlarm` -> `SessionStreamController::DoAlarmTimeoutDetection`:
        1. `LossDetectionAlgo::DetectLoss`
        2. `CongestionCtlAlgo::OnDataAckOrLoss`: `RenoCongestionContrl::OnDataLoss`
        3. `SessionStreamController::InformLossUp`
    5. `SessionStreamController::OnDataPktReceived`
        1. `RttStats::UpdateRtt`
        2. `RenoCongestionContrl::OnDataRecv`
        3. `RenoCongestionContrl::GetCWND`
    6. `MPDTransportController::OnDataPiecesReceived` -> `RRMultiPathScheduler::OnReceiveSubpieceData` -> `DoSinglePathSchedule`
6. `MPD::StartTesting`

- rid: task id
- remote_peerid: server id, session id

DoSinglePathSchedule: m_downloadPieces -> m_downloadQueue -> vecSubpieceNums -> m_session_needdownloadpieceQ -> vecSubpieces -> spn

测试环境：

- 一个包的大小为 1KB，测试中总共需要 10MB 的数据，共 10240 个数据包
- 视频播放的过程中比特流为 128KBps，播放时间为 80s，每秒需要 128 个包
- 10 秒需要 1280 个包，共 1280KBps
