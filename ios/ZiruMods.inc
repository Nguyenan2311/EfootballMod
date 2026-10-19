// ZIP add-ons are staged while the game runs, then activated before the next UE launch.
@interface ZiruModVC : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, strong) UITextView* inventory;
@property(nonatomic, strong) UIButton* pickMod;
@end

static NSString* modInventory() {
  NSFileManager* fm = NSFileManager.defaultManager;
  NSMutableString* text = [NSMutableString stringWithString:@"Sân đã có đủ bộ file trên máy:\n"];
  NSString* dl = [gDocs stringByAppendingPathComponent:@"Download"];
  int count = 0;
  for (NSString* n in [[fm contentsOfDirectoryAtPath:dl error:nil] sortedArrayUsingSelector:@selector(compare:)]) {
    if (!zm::stadiumName(n.UTF8String) || ![n hasSuffix:@"_mobile_ios.pak"]) continue;
    NSString* stem = n.stringByDeletingPathExtension;
    BOOL complete = YES;
    for (NSString* ext in @[@"pak", @"ucas", @"utoc"]) {
      NSString* p = [dl stringByAppendingPathComponent:[stem stringByAppendingPathExtension:ext]];
      NSDictionary* a = [fm attributesOfItemAtPath:p.stringByResolvingSymlinksInPath error:nil];
      if ([a[NSFileSize] unsignedLongLongValue] == 0) complete = NO;
    }
    [text appendFormat:@"• %@ — %@\n", stem, complete ? @"đủ 3 file" : @"thiếu file đi kèm"];
    ++count;
  }
  if (!count) [text appendString:@"Chưa có bộ sân iOS trong Download.\n"];
  NSArray* batches = [fm contentsOfDirectoryAtPath:[gSupport stringByAppendingPathComponent:@"PendingMods"] error:nil];
  for (NSString* b in batches) if (![b hasPrefix:@"."]) [text appendFormat:@"\nMod chờ khởi động lại: %@", b];
  [text appendString:@"\n\nChọn ZIP có file .pak/.ucas/.utoc/.cpk, đặt trực tiếp hoặc trong pak, cpk, Download, livedata. Sân pc3xxx cần đủ cả 3 file trong cùng ZIP.\n\nDùng tài nguyên tương thích iOS. Đổi tên file Android sang iOS không chuyển đổi texture/shader.\n\nSau khi nhập xong, đóng hẳn game rồi mở lại. File cũ được giữ trong Documents/ModBackups. Bạn cũng có thể tự chép mod vào các thư mục dữ liệu trong ứng dụng Tệp."];
  return text;
}

@implementation ZiruModVC
- (void)viewDidLoad {
  [super viewDidLoad]; self.title = @"Cài thêm mod";
  self.view.backgroundColor = PESNight();
  self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Đóng" style:UIBarButtonItemStyleDone target:self action:@selector(closeMods)];
  self.pickMod = [UIButton buttonWithType:UIButtonTypeSystem];
  [self.pickMod setTitle:@"Chọn ZIP mod từ Tệp" forState:UIControlStateNormal];
  self.pickMod.backgroundColor = PESLime(); self.pickMod.tintColor = PESNight(); self.pickMod.layer.cornerRadius = 10;
  [self.pickMod addTarget:self action:@selector(chooseMod) forControlEvents:UIControlEventTouchUpInside];
  self.inventory = [UITextView new]; self.inventory.editable = NO; self.inventory.backgroundColor = UIColor.clearColor;
  self.inventory.textColor = UIColor.whiteColor; self.inventory.font = [UIFont systemFontOfSize:16]; self.inventory.text = modInventory();
  for (UIView* v in @[self.pickMod, self.inventory]) { v.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:v]; }
  UILayoutGuide* g = self.view.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [self.pickMod.topAnchor constraintEqualToAnchor:g.topAnchor constant:12],
    [self.pickMod.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:20],
    [self.pickMod.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-20],
    [self.pickMod.heightAnchor constraintEqualToConstant:44],
    [self.inventory.topAnchor constraintEqualToAnchor:self.pickMod.bottomAnchor constant:12],
    [self.inventory.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:16],
    [self.inventory.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-16],
    [self.inventory.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-12]]];
}
- (void)closeMods { [self dismissViewControllerAnimated:YES completion:nil]; }
- (void)chooseMod {
  UIDocumentPickerViewController* p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeZIP] asCopy:NO];
  p.delegate = self; [self presentViewController:p animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  NSURL* u = urls.firstObject; if (!u) return;
  BOOL scoped = [u startAccessingSecurityScopedResource];
  self.pickMod.enabled = NO; self.navigationItem.rightBarButtonItem.enabled = NO;
  self.inventory.text = @"Đang giải nén và kiểm tra file mod…\nVui lòng giữ ứng dụng mở.";
  BOOL wasIdleDisabled = UIApplication.sharedApplication.idleTimerDisabled;
  UIApplication.sharedApplication.idleTimerDisabled = YES;
  uint64_t available = freeBytes();
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    std::string summary, error;
    bool ok = false;
    try {
      ok = zm::stageZip(u.path.fileSystemRepresentation,
          [gSupport stringByAppendingPathComponent:@"PendingMods"].fileSystemRepresentation, available, summary, error);
    } catch (const std::exception& e) { error = e.what(); }
    if (scoped) [u stopAccessingSecurityScopedResource];
    ZLog(@"[MOD] stage %s: %s", ok ? "OK" : "FAILED", ok ? summary.c_str() : error.c_str());
    dispatch_async(dispatch_get_main_queue(), ^{
      UIApplication.sharedApplication.idleTimerDisabled = wasIdleDisabled;
      self.pickMod.enabled = YES; self.navigationItem.rightBarButtonItem.enabled = YES;
      self.inventory.text = modInventory();
      NSString* msg = ok ? @"Đã kiểm tra và chuẩn bị mod. Hãy đóng hẳn game rồi mở lại để áp dụng."
          : [NSString stringWithUTF8String:error.c_str()];
      UIAlertController* a = [UIAlertController alertControllerWithTitle:ok ? @"Mod đã sẵn sàng" : @"Chưa nhập được mod" message:msg preferredStyle:UIAlertControllerStyleAlert];
      [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
      [self presentViewController:a animated:YES completion:nil];
    });
  });
}
@end

@interface ZiruModButtonTarget : NSObject
@end
@implementation ZiruModButtonTarget
- (void)openMods:(UIButton*)button {
  UIViewController* vc = button.window.rootViewController;
  while (vc.presentedViewController) vc = vc.presentedViewController;
  if (!vc || ([vc isKindOfClass:UINavigationController.class] && [((UINavigationController*)vc).topViewController isKindOfClass:ZiruModVC.class])) return;
  UINavigationController* nav = [[UINavigationController alloc] initWithRootViewController:[ZiruModVC new]];
  nav.modalPresentationStyle = UIModalPresentationPageSheet; [vc presentViewController:nav animated:YES completion:nil];
}
@end
static void installModButton(UIWindow* win) {
  (void)win;
  // MOD button hidden per user request: mod files are added directly to the game directory
}
static void showModError(UIWindow* win, NSString* error) {
  UIViewController* vc = win.rootViewController; while (vc.presentedViewController) vc = vc.presentedViewController;
  UIAlertController* a = [UIAlertController alertControllerWithTitle:@"Mod chưa được áp dụng" message:error preferredStyle:UIAlertControllerStyleAlert];
  [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
  [vc presentViewController:a animated:YES completion:nil];
}
