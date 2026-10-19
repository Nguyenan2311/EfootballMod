package android.database;
public interface Cursor extends java.io.Closeable {
  boolean moveToNext(); boolean moveToFirst(); String getString(int i); int getInt(int i); long getLong(int i); int getCount(); void close();
}
