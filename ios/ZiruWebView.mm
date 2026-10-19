// Local HTML pages from Ziru's server responses on iOS.
//
// CmdGetServerEnv (Ziru, Android) points the privacy/terms/notice pages at file:///android_asset/responses/<id>.html.
// On iOS WKWebView refuses that URL ("Ignoring request to load this main resource because it is outside the
// sandbox"). The same HTML files ship in the IPA at PESmobile.app/ziru/responses/, so -[WKWebView loadRequest:] is
// swizzled to load them with loadFileURL:allowingReadAccessToURL: instead. Every other request is untouched.
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

#include "ZiruWebView.h"

static void (*gWebLog)(const char*) = nullptr;
static IMP gOrigLoadRequest = nullptr;

static NSURL* ziruLocalURLFor(NSURL* u) {
  if (!u) return nil;
  NSString* path = nil;
  if (u.isFileURL && [u.path hasPrefix:@"/android_asset/"]) {
    path = [u.path substringFromIndex:@"/android_asset/".length];   // responses/<id>.html
  } else if ([u.scheme.lowercaseString isEqualToString:@"file"] && [u.absoluteString containsString:@"android_asset/"]) {
    NSRange r = [u.absoluteString rangeOfString:@"android_asset/"];
    path = [[u.absoluteString substringFromIndex:NSMaxRange(r)] stringByRemovingPercentEncoding];
    NSRange cut = [path rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"?#"]];
    if (cut.location != NSNotFound) path = [path substringToIndex:cut.location];
  }
  if (!path.length || [path containsString:@".."]) return nil;
  NSString* full = [[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"ziru"] stringByAppendingPathComponent:path];
  if (![NSFileManager.defaultManager fileExistsAtPath:full]) return nil;
  NSURL* f = [NSURL fileURLWithPath:full];
  if (u.fragment.length) {
    NSURLComponents* c = [NSURLComponents componentsWithURL:f resolvingAgainstBaseURL:NO];
    c.fragment = u.fragment;
    f = c.URL ?: f;
  }
  return f;
}

static WKNavigation* ziruLoadRequest(WKWebView* self, SEL _cmd, NSURLRequest* request) {
  NSURL* local = ziruLocalURLFor(request.URL);
  if (local) {
    if (gWebLog) {
      NSString* m = [NSString stringWithFormat:@"[WEB] %@ -> %@", request.URL.absoluteString, local.path.lastPathComponent];
      gWebLog(m.UTF8String);
    }
    NSURL* dir = [local URLByDeletingLastPathComponent];
    return [self loadFileURL:local allowingReadAccessToURL:dir];
  }
  return ((WKNavigation * (*)(id, SEL, NSURLRequest*)) gOrigLoadRequest)(self, _cmd, request);
}

namespace zweb {
void install(void (*log)(const char* line)) {
  gWebLog = log;
  Method m = class_getInstanceMethod(WKWebView.class, @selector(loadRequest:));
  if (!m) {
    if (log) log("[WEB] WKWebView loadRequest: not found");
    return;
  }
  gOrigLoadRequest = method_setImplementation(m, (IMP)ziruLoadRequest);
  if (log) log("[WEB] file:///android_asset/* pages -> PESmobile.app/ziru/*");
}
}  // namespace zweb
