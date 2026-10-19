package android.content.res;
import java.io.*;
public class AssetManager {
  final File root; public AssetManager(File r){root=r;}
  public InputStream open(String n) throws IOException { File f=new File(root,n); if(!f.isFile()) throw new FileNotFoundException(n); return new FileInputStream(f); }
  public String[] list(String n) throws IOException { File f=new File(root,n); String[] l=f.list(); if(l==null) return new String[0]; java.util.Arrays.sort(l); return l; }
}
