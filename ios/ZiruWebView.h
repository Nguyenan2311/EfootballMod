// Maps Ziru's file:///android_asset/... web pages to the copies inside the IPA (see ZiruWebView.mm).
#pragma once
namespace zweb {
void install(void (*log)(const char* line));
}
