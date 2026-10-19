package android.util;
public final class Base64 {
  public static final int DEFAULT=0, NO_PADDING=1, NO_WRAP=2, URL_SAFE=8;
  public static String encodeToString(byte[] b,int flags){ java.util.Base64.Encoder e = (flags&URL_SAFE)!=0? java.util.Base64.getUrlEncoder(): java.util.Base64.getEncoder(); if((flags&NO_PADDING)!=0) e=e.withoutPadding(); String s=e.encodeToString(b); if((flags&NO_WRAP)==0){ StringBuilder sb=new StringBuilder(); for(int i=0;i<s.length();i+=76){ sb.append(s,i,Math.min(s.length(),i+76)).append('\n'); } s=sb.toString(); } return s; }
  public static byte[] decode(String s,int flags){ return java.util.Base64.getMimeDecoder().decode(s); }
}
