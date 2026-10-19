package android.database.sqlite;
import java.sql.*; import java.util.*;
import android.content.ContentValues; import android.database.Cursor;
public class SQLiteDatabase {
  public interface CursorFactory {}
  public static final int CONFLICT_REPLACE=5;
  final Connection c; int depth=0; boolean ok=false;
  SQLiteDatabase(Connection c){this.c=c;}
  public void execSQL(String s){ try(Statement st=c.createStatement()){st.execute(s);}catch(SQLException e){throw new RuntimeException(e);} }
  public void execSQL(String s,Object[] a){ try(PreparedStatement st=c.prepareStatement(s)){ for(int i=0;i<a.length;i++) st.setObject(i+1,a[i]); st.execute(); }catch(SQLException e){throw new RuntimeException(e);} }
  public Cursor rawQuery(String s,String[] a){ try{ PreparedStatement st=c.prepareStatement(s); if(a!=null) for(int i=0;i<a.length;i++) st.setString(i+1,a[i]); return new android.database.JdbcCursor(st, st.executeQuery()); }catch(SQLException e){throw new RuntimeException(e);} }
  public long insertWithOnConflict(String t,String n,ContentValues v,int mode){ StringBuilder k=new StringBuilder(), q=new StringBuilder(); List<Object> vals=new ArrayList<>(); for(Map.Entry<String,Object> e:v.m.entrySet()){ if(k.length()>0){k.append(',');q.append(',');} k.append(e.getKey()); q.append('?'); vals.add(e.getValue()); }
    String s="INSERT OR REPLACE INTO "+t+"("+k+") VALUES("+q+")"; try(PreparedStatement st=c.prepareStatement(s)){ for(int i=0;i<vals.size();i++) st.setObject(i+1,vals.get(i)); st.executeUpdate(); try(Statement s2=c.createStatement(); ResultSet r=s2.executeQuery("SELECT last_insert_rowid()")){ r.next(); return r.getLong(1);} }catch(SQLException e){throw new RuntimeException(e);} }
  public long insert(String t,String n,ContentValues v){ return insertWithOnConflict(t,n,v,0); }
  public int delete(String t,String w,String[] a){ try(PreparedStatement st=c.prepareStatement("DELETE FROM "+t+(w!=null?" WHERE "+w:""))){ if(a!=null) for(int i=0;i<a.length;i++) st.setString(i+1,a[i]); return st.executeUpdate(); }catch(SQLException e){throw new RuntimeException(e);} }
  public void beginTransaction(){ try{ if(depth++==0){ c.setAutoCommit(false); ok=false;} }catch(SQLException e){throw new RuntimeException(e);} }
  public void setTransactionSuccessful(){ ok=true; }
  public void endTransaction(){ try{ if(--depth==0){ if(ok) c.commit(); else c.rollback(); c.setAutoCommit(true);} }catch(SQLException e){throw new RuntimeException(e);} }
}
