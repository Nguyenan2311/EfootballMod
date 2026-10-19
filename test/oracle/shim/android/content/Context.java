package android.content;
import java.io.*; import java.util.*;
import android.content.res.AssetManager;
public class Context {
  public final File assetsDir, filesDir; final Map<String,Prefs> prefs=new HashMap<>();
  public Context(File assets, File files){assetsDir=assets; filesDir=files; files.mkdirs();}
  public AssetManager getAssets(){ return new AssetManager(assetsDir); }
  public File getFilesDir(){ return filesDir; }
  public File getExternalFilesDir(String t){ return null; }
  public File getDatabasePath(String n){ return new File(filesDir, n); }
  public SharedPreferences getSharedPreferences(String n,int mode){ return prefs.computeIfAbsent(n, x->new Prefs(new File(filesDir,"prefs_"+x+".properties"))); }
  static class Prefs implements SharedPreferences {
    final File f; final Properties p=new Properties(); final Map<String,Object> m=new LinkedHashMap<>();
    Prefs(File f){this.f=f; if(f.isFile()) try(Reader r=new InputStreamReader(new FileInputStream(f),"UTF-8")){p.load(r); for(String k:p.stringPropertyNames()){ String v=p.getProperty(k); int c=v.indexOf(':'); String t=v.substring(0,c), s=v.substring(c+1); m.put(k, t.equals("s")?s: t.equals("i")?(Object)Integer.valueOf(s): t.equals("l")?(Object)Long.valueOf(s):(Object)Boolean.valueOf(s)); }}catch(IOException e){throw new RuntimeException(e);} }
    void save(){ Properties q=new Properties(); for(Map.Entry<String,Object> e:m.entrySet()){ Object v=e.getValue(); String t= v instanceof String?"s": v instanceof Integer?"i": v instanceof Long?"l":"b"; q.setProperty(e.getKey(), t+":"+v);} try(Writer w=new OutputStreamWriter(new FileOutputStream(f),"UTF-8")){q.store(w,null);}catch(IOException e){throw new RuntimeException(e);} }
    public String getString(String k,String d){Object v=m.get(k); return v instanceof String?(String)v:d;}
    public int getInt(String k,int d){Object v=m.get(k); return v instanceof Integer?(Integer)v:d;}
    public long getLong(String k,long d){Object v=m.get(k); return v instanceof Long?(Long)v:d;}
    public boolean getBoolean(String k,boolean d){Object v=m.get(k); return v instanceof Boolean?(Boolean)v:d;}
    public boolean contains(String k){return m.containsKey(k);}
    public Editor edit(){ final Prefs P=this; return new Editor(){ final Map<String,Object> ch=new LinkedHashMap<>(); boolean clr=false;
      public Editor putString(String k,String v){ch.put(k,v);return this;} public Editor putInt(String k,int v){ch.put(k,v);return this;}
      public Editor putLong(String k,long v){ch.put(k,v);return this;} public Editor putBoolean(String k,boolean v){ch.put(k,v);return this;}
      public Editor remove(String k){ch.put(k,null);return this;} public Editor clear(){clr=true;return this;}
      public boolean commit(){ if(clr) P.m.clear(); for(Map.Entry<String,Object> e:ch.entrySet()){ if(e.getValue()==null) P.m.remove(e.getKey()); else P.m.put(e.getKey(),e.getValue()); } P.save(); return true; }
      public void apply(){commit();} }; }
  }
}
