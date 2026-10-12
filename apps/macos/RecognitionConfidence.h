#pragma once

// 识别置信度闸门（识别即办理策略的一部分）。
//
// 管理端默认「识别成功就直接入场/出场」，只在**读得不够准**时弹一次人工确认。
// 判据只取识别置信度（recognition confidence，即"车牌读对没读对"），不把检测
// 置信度也算进来：样张上检测常常只有 0.8 出头而识别是 0.99，用两者的 min() 会把
// 好读的车牌也拦下来，反而变回"每辆车都要点一下"。
//
// 阈值由调用方给（默认见 OperationsViewController 的 0.85），识别结果自带
// `valid=false` 时无条件要求确认。
namespace smartpark_lpr{

inline bool needsConfirmation(double recognitionConfidence, bool valid,
                              double threshold){
    if (!valid){
        return true;
    }
    return recognitionConfidence < threshold;
}

} // namespace smartpark_lpr
