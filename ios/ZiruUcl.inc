// Champions League screen: a small draggable "UCL" button over the game opens ZIRU_UCL_PAGE.html (all groups,
// bracket, scorers and assists, MyLeague mode switch), filled by the offline server (srv_ucl.inc).

@interface ZiruUclVC : UIViewController <WKScriptMessageHandler>
@property(nonatomic, strong) WKWebView* web;
@end

@implementation ZiruUclVC
- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = [UIColor colorWithRed:0.04 green:0.06 blue:0.15 alpha:1];
  WKWebViewConfiguration* cfg = [WKWebViewConfiguration new];
  [cfg.userContentController addScriptMessageHandler:self name:@"ziru"];
  self.web = [[WKWebView alloc] initWithFrame:self.view.bounds configuration:cfg];
  self.web.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.web.opaque = NO;
  self.web.backgroundColor = self.view.backgroundColor;
  [self.view addSubview:self.web];
  [self reload];
}
- (void)reload {
  std::string html;
  try {
    std::lock_guard<std::recursive_mutex> g(server().mu);
    html = server().uclPageHtml();
  } catch (const std::exception& e) {
    html = std::string("<body style='background:#0b1026;color:#fff;font:15px sans-serif'>") + e.what();
  }
  [self.web loadHTMLString:[NSString stringWithUTF8String:html.c_str()] ?: @"" baseURL:nil];
}
- (void)userContentController:(WKUserContentController*)uc didReceiveScriptMessage:(WKScriptMessage*)msg {
  NSString* body = [msg.body isKindOfClass:NSString.class] ? msg.body : @"";
  if ([body isEqualToString:@"close"]) {
    [self dismissViewControllerAnimated:YES completion:nil];
  } else if ([body hasPrefix:@"mode:"]) {
    try {
      std::lock_guard<std::recursive_mutex> g(server().mu);
      server().uclSetMode([body substringFromIndex:5].UTF8String);
    } catch (const std::exception& e) {
      ZLog(@"[UCL] mode switch failed: %s", e.what());
    }
    [self reload];
  }
}
- (void)viewDidDisappear:(BOOL)animated {
  [super viewDidDisappear:animated];
  [self.web.configuration.userContentController removeScriptMessageHandlerForName:@"ziru"];  // it retains self
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

@interface ZiruUclButton : UIButton
@end

@implementation ZiruUclButton
- (void)drag:(UIPanGestureRecognizer*)g {
  UIView* sv = self.superview;
  CGPoint t = [g translationInView:sv];
  CGFloat r = self.bounds.size.width / 2;
  self.center = CGPointMake(MIN(MAX(self.center.x + t.x, r), sv.bounds.size.width - r),
                            MIN(MAX(self.center.y + t.y, r), sv.bounds.size.height - r));
  [g setTranslation:CGPointZero inView:sv];
}
- (void)open {
  UIViewController* top = self.window.rootViewController;
  while (top.presentedViewController) top = top.presentedViewController;
  if (!top || [top isKindOfClass:ZiruUclVC.class]) return;
  ZiruUclVC* vc = [ZiruUclVC new];
  vc.modalPresentationStyle = UIModalPresentationFullScreen;
  [top presentViewController:vc animated:YES completion:nil];
}
- (void)raise { [self.superview bringSubviewToFront:self]; }  // Unreal adds its own views above ours
@end

static void installUclButton(UIWindow* win) {
  if (!win || [win viewWithTag:0x2C1]) return;
  ZiruUclButton* b = [ZiruUclButton buttonWithType:UIButtonTypeCustom];
  b.tag = 0x2C1;
  b.frame = CGRectMake(win.bounds.size.width - 64, 64, 44, 44);
  b.layer.cornerRadius = 22;
  b.layer.borderWidth = 1;
  b.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.35].CGColor;
  b.backgroundColor = [UIColor colorWithRed:0.10 green:0.20 blue:0.55 alpha:0.75];
  b.titleLabel.font = [UIFont boldSystemFontOfSize:12];
  b.accessibilityLabel = @"Champions League";
  [b setTitle:@"UCL" forState:UIControlStateNormal];
  [b addTarget:b action:@selector(open) forControlEvents:UIControlEventTouchUpInside];
  [b addGestureRecognizer:[[UIPanGestureRecognizer alloc] initWithTarget:b action:@selector(drag:)]];
  [win addSubview:b];
  [NSTimer scheduledTimerWithTimeInterval:2 target:b selector:@selector(raise) userInfo:nil repeats:YES];
  ZLog(@"[UCL] button installed");
}
