# Distinguishing Receive Timeouts from Channel Errors

## Overview

Input channels previously represented both a receive timeout and a channel failure as zero bytes. This made normal idle periods indistinguishable from invalid sockets, failed polling operations, or unavailable channel resources.

The receive API now reports data, timeout, closure, and errors as distinct statuses for single-message and batch reads. Error results retain a `std::error_code` where the underlying operation provides one.

## Design Decisions

- Use a typed status and value result so receive byte counts and batch counts remain explicit.
- Treat timeouts as normal polling outcomes, while closed and error states remain visible to callers.
- Retain successfully read batch counts if a later read in the same drain fails.
- Preserve existing batch buffering and dispatch behavior for successful reads.

## Tasks / Checklist

- [x] Add shared typed receive result types and update the input-channel interface.
- [x] Propagate receive status through socket pollers and concrete input channels.
- [x] Update execution strategies and tests to distinguish timeout, closure, and errors.
- [x] Remove unused includes from touched headers and source files.
