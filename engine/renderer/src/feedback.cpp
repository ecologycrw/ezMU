#include <ezmu/renderer/feedback.hpp>

#include <algorithm>
#include <utility>

namespace ezmu::renderer {

RenderFeedbackQueue::RenderFeedbackQueue(std::uint32_t reportCapacity)
    : m_reports(std::max<std::uint32_t>(reportCapacity, 1)) {}

void RenderFeedbackQueue::pushReport(const FrameReport& report) {
    const std::scoped_lock lock(m_mutex);
    const auto capacity = static_cast<std::uint32_t>(m_reports.size());
    if (m_reportCount == capacity) {
        // Full: drop the oldest report.
        m_reportHead = (m_reportHead + 1) % capacity;
        --m_reportCount;
        ++m_droppedReports;
    }
    m_reports.at((m_reportHead + m_reportCount) % capacity) = report;
    ++m_reportCount;
}

void RenderFeedbackQueue::pushCapture(CaptureResult result) {
    const std::scoped_lock lock(m_mutex);
    m_captures.push_back(std::move(result));
}

void RenderFeedbackQueue::pushFatalError(foundation::Error error) {
    const std::scoped_lock lock(m_mutex);
    if (!m_fatalError) {
        m_fatalError = std::move(error);
    }
}

void RenderFeedbackQueue::drain(Drained& out) {
    out.reports.clear();
    out.captures.clear();
    out.fatalError.reset();
    out.droppedReports = 0;

    const std::scoped_lock lock(m_mutex);
    const auto capacity = static_cast<std::uint32_t>(m_reports.size());
    for (std::uint32_t i = 0; i < m_reportCount; ++i) {
        out.reports.push_back(m_reports.at((m_reportHead + i) % capacity));
    }
    m_reportHead = 0;
    m_reportCount = 0;
    // Swap so both sides keep their capacity: `out.captures` is empty after clear().
    std::swap(out.captures, m_captures);
    out.fatalError = std::exchange(m_fatalError, std::nullopt);
    out.droppedReports = std::exchange(m_droppedReports, 0);
}

} // namespace ezmu::renderer
