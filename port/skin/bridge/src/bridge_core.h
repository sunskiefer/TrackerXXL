/* Core headers with access to what the display stack keeps private (controlsTable, the controls' members, the modules' handles).
 * The bridge only READS the firmware's state; the hack is local to the bridge sources (same layout, no behavioural change). */
#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>
#include <time.h>
#include <ranges>      /* C++20 headers the synth layer pulls in: include them before the private/public hack */
#include <span>
#include <array>
#include <optional>
#include <numeric>
#include <string_view>
#include <variant>
#include <chrono>
#include <bitset>
#define private public
#define protected public
#include "display.h"
#include "core/interface.h"
#include "core/titleBar.h"
#include "core/songTimer.h"
#include "core/interfacePopups.h"
#include "core/interfaceMenuPopup.h"
#include "patternEditor.h"
#include "sampleEditor.h"
#include "sampleImporter.h"
#include "samplePlayback.h"
#include "sampleRecorder.h"
#include "instrumentEditor.h"
#include "songEditor.h"
#include "projectEditor.h"
#include "configEditor.h"
#include "masterParams.h"
#include "performanceMode.h"
#include "imageViewer.h"
#undef private
#undef protected
extern void onButtonPush(uint8_t n);
extern void onButtonRelease(uint8_t n);
