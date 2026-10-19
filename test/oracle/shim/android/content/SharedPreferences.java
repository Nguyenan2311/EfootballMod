package android.content;
public interface SharedPreferences {
  String getString(String k,String d); int getInt(String k,int d); long getLong(String k,long d); boolean getBoolean(String k,boolean d); boolean contains(String k);
  Editor edit();
  interface Editor { Editor putString(String k,String v); Editor putInt(String k,int v); Editor putLong(String k,long v); Editor putBoolean(String k,boolean v); Editor remove(String k); Editor clear(); boolean commit(); void apply(); }
}
