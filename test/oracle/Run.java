import java.io.*; import java.util.*; import java.lang.reflect.*;
import fi.iki.elonen.NanoHTTPD;
// Reads TSV script lines: method \t uri \t base64body(or -) ; writes: status \t mime \t base64body
public class Run {
  public static void main(String[] a) throws Exception {
    android.content.Context ctx = new android.content.Context(new File(a[0]), new File(a[1]));
    Class<?> c = Class.forName("com.offline.server.OfflineServer");
    Object srv = c.getConstructor(int.class, android.content.Context.class).newInstance(8085, ctx);
    Method serve = c.getMethod("serve", NanoHTTPD.IHTTPSession.class);
    BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(a[2]), "UTF-8"));
    PrintStream out = new PrintStream(new FileOutputStream(a[3]), true, "UTF-8");
    String line;
    while ((line = in.readLine()) != null) {
      if (line.isEmpty()) continue;
      String[] p = line.split("\t", -1);
      if (p[0].equals("CLOCK")) { zshim.Det.clock = Long.parseLong(p[1]); continue; }
      final String method = p[0], uri = p[1];
      final byte[] body = p[2].equals("-") ? new byte[0] : Base64.getDecoder().decode(p[2]);
      final Map<String,String> h = new HashMap<>();
      if (body.length > 0) h.put("content-length", String.valueOf(body.length));
      final InputStream is = new ByteArrayInputStream(body);
      NanoHTTPD.IHTTPSession s = new NanoHTTPD.IHTTPSession() {
        public String getUri() { return uri; }
        public NanoHTTPD.Method getMethod() { return NanoHTTPD.Method.valueOf(method); }
        public Map<String,String> getHeaders() { return h; }
        public InputStream getInputStream() { return is; }
        public void execute() {}
        public NanoHTTPD.CookieHandler getCookies() { return null; }
        public Map<String, List<String>> getParameters() { return new HashMap<>(); }
        public Map<String,String> getParms() { return new HashMap<>(); }
        public String getQueryParameterString() { return null; }
        public String getRemoteHostName() { return "localhost"; }
        public String getRemoteIpAddress() { return "127.0.0.1"; }
        public void parseBody(Map<String,String> m) {}
      };
      NanoHTTPD.Response r;
      try { r = (NanoHTTPD.Response) serve.invoke(srv, s); }
      catch (InvocationTargetException e) { out.println("EXC\t" + e.getCause() + "\t-"); continue; }
      if (r == null) { out.println("NULL\t-\t-"); continue; }
      byte[] d = r.getData() == null ? new byte[0] : r.getData().readAllBytes();
      out.println(r.getStatus().getRequestStatus() + "\t" + r.getMimeType() + "\t" + (d.length == 0 ? "-" : Base64.getEncoder().encodeToString(d)));
    }
  }
}
