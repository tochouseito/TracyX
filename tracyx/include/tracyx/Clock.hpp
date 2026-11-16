#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tracyx
{

struct NormalizedTime
{
    uint64_t nanoseconds = 0;
    uint64_t uncertaintyNs = 0;
};

// セッション開始 QPC を原点にする。起点と周波数が未設定なら変換しない。
class QpcClock
{
public:
    QpcClock( uint64_t frequency, uint64_t origin );
    std::optional<NormalizedTime> Normalize( uint64_t qpc ) const;

private:
    uint64_t m_frequency;
    uint64_t m_origin;
};

struct ClockAnchor
{
    uint64_t qpcBefore = 0;
    uint64_t qpcAfter = 0;
    uint64_t tracyNs = 0;
};

// Tracy 時刻への変換は複数の FrameMark アンカーからのみ確定する。
class TracyClockFit
{
public:
    explicit TracyClockFit( uint64_t qpcFrequency );
    bool AddAnchor( ClockAnchor anchor );
    std::optional<NormalizedTime> ToTracyNs( uint64_t qpc ) const;
    uint64_t MaxResidualNs() const;
    size_t AnchorCount() const;

private:
    uint64_t m_frequency;
    std::vector<ClockAnchor> m_anchors;
};

} // namespace tracyx
