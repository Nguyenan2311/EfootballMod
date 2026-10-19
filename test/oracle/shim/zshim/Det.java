package zshim;
public class Det { public static long clock = Long.getLong("zclock", 0L); public static long now(){ return clock != 0 ? clock : System.currentTimeMillis(); } }
