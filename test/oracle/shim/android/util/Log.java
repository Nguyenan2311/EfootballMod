package android.util;
public final class Log {
  public static boolean QUIET = System.getenv("ZLOG") == null;
  static int p(String l, String t, String m, Throwable e){ if(!QUIET){ System.err.println(l+"/"+t+": "+m); if(e!=null) e.printStackTrace(); } return 0; }
  public static int i(String t,String m){return p("I",t,m,null);} public static int d(String t,String m){return p("D",t,m,null);}
  public static int w(String t,String m){return p("W",t,m,null);} public static int w(String t,String m,Throwable e){return p("W",t,m,e);}
  public static int e(String t,String m){return p("E",t,m,null);} public static int e(String t,String m,Throwable e){return p("E",t,m,e);}
  public static int v(String t,String m){return p("V",t,m,null);}
}
