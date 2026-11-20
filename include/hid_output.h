#pragma once

#include <cstdint>

namespace lumina {

// hid_report_type_t values (TinyUSB class/hid/hid.h)
static constexpr uint8_t HID_TYPE_INVALID = 0;
static constexpr uint8_t HID_TYPE_INPUT = 1;
static constexpr uint8_t HID_TYPE_OUTPUT = 2;
static constexpr uint8_t HID_TYPE_FEATURE = 3;

struct OutputReport {
  uint8_t id;
  const uint8_t *data;
  uint16_t len;
};

/**
 * Normalises the arguments of TinyUSB's tud_hid_set_report_cb into (report ID, payload).
 *
 * Every profile descriptor uses report IDs. Data from the interrupt OUT endpoint arrives with
 * report_id 0 and the real ID as its first byte; TinyUSB 0.16 passes report_type INVALID for it,
 * newer versions pass OUTPUT. SET_REPORT(Output) on the control pipe carries the ID in report_id,
 * and depending on the TinyUSB version also as the first byte.
 * Returns false for anything that is not an output report.
 */
inline bool parseOutputReport(uint8_t reportId, uint8_t reportType, const uint8_t *buffer, uint16_t bufsize, OutputReport &out) {
  if (reportType != HID_TYPE_OUTPUT && reportType != HID_TYPE_INVALID) {
    return false;
  }
  if (reportId == 0) {
    if (bufsize == 0) {
      return false;
    }
    out = {buffer[0], buffer + 1, static_cast<uint16_t>(bufsize - 1)};
    return true;
  }
  if (bufsize > 0 && buffer[0] == reportId) {
    buffer++;
    bufsize--;
  }
  out = {reportId, buffer, bufsize};
  return true;
}

}  // namespace lumina
