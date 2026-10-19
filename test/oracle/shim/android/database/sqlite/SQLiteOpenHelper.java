package android.database.sqlite;
import java.sql.*; import android.content.Context;
public abstract class SQLiteOpenHelper {
  final Context ctx; final String name; final int ver; SQLiteDatabase db;
  public SQLiteOpenHelper(Context c,String n,SQLiteDatabase.CursorFactory f,int v){ctx=c;name=n;ver=v;}
  public abstract void onCreate(SQLiteDatabase db); public abstract void onUpgrade(SQLiteDatabase db,int o,int n);
  public synchronized SQLiteDatabase getWritableDatabase(){ if(db==null){ try{ Connection c=DriverManager.getConnection("jdbc:sqlite:"+ctx.getDatabasePath(name).getAbsolutePath()); db=new SQLiteDatabase(c); int cur; try(Statement s=c.createStatement(); ResultSet r=s.executeQuery("PRAGMA user_version")){ r.next(); cur=r.getInt(1);} if(cur==0) onCreate(db); else if(cur<ver) onUpgrade(db,cur,ver); try(Statement s=c.createStatement()){ s.execute("PRAGMA user_version="+ver);} }catch(SQLException e){throw new RuntimeException(e);} } return db; }
  public SQLiteDatabase getReadableDatabase(){ return getWritableDatabase(); }
}
