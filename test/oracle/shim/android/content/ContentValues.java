package android.content;
import java.util.*;
public class ContentValues { public final LinkedHashMap<String,Object> m=new LinkedHashMap<>();
  public void put(String k,String v){m.put(k,v);} public void put(String k,Long v){m.put(k,v);} public void put(String k,Integer v){m.put(k,v);} public void put(String k,Double v){m.put(k,v);} public void put(String k,Boolean v){m.put(k,v);} }
