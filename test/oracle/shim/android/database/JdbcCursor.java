package android.database;
import java.sql.*;
public class JdbcCursor implements Cursor { final ResultSet rs; final Statement st;
  public JdbcCursor(Statement st, ResultSet rs){this.st=st;this.rs=rs;}
  public boolean moveToNext(){ try{return rs.next();}catch(SQLException e){throw new RuntimeException(e);} }
  public boolean moveToFirst(){ return moveToNext(); }
  public String getString(int i){ try{return rs.getString(i+1);}catch(SQLException e){throw new RuntimeException(e);} }
  public int getInt(int i){ try{return rs.getInt(i+1);}catch(SQLException e){throw new RuntimeException(e);} }
  public long getLong(int i){ try{return rs.getLong(i+1);}catch(SQLException e){throw new RuntimeException(e);} }
  public int getCount(){ return -1; }
  public void close(){ try{rs.close(); st.close();}catch(SQLException e){} }
}
