import org.objectweb.asm.*;
import java.nio.file.*; import java.util.zip.*; import java.io.*;
public class Patch {
  public static void main(String[] a) throws Exception {
    try (ZipInputStream in = new ZipInputStream(new FileInputStream(a[0])); ZipOutputStream out = new ZipOutputStream(new FileOutputStream(a[1]))) {
      ZipEntry e;
      while ((e = in.getNextEntry()) != null) {
        byte[] b = in.readAllBytes();
        if (e.getName().startsWith("com/offline/server/") && e.getName().endsWith(".class")) {
          ClassReader cr = new ClassReader(b);
          ClassWriter cw = new ClassWriter(0);
          cr.accept(new ClassVisitor(Opcodes.ASM9, cw) {
            public MethodVisitor visitMethod(int acc, String n, String d, String s, String[] x) {
              return new MethodVisitor(Opcodes.ASM9, super.visitMethod(acc, n, d, s, x)) {
                public void visitMethodInsn(int op, String owner, String name, String desc, boolean itf) {
                  if (owner.equals("java/lang/System") && name.equals("currentTimeMillis")) { super.visitMethodInsn(Opcodes.INVOKESTATIC, "zshim/Det", "now", "()J", false); return; }
                  if (owner.equals("java/util/Random") && name.equals("<init>") && desc.equals("()V")) { super.visitMethodInsn(op, "zshim/DetRandom", name, desc, itf); return; }
                  super.visitMethodInsn(op, owner, name, desc, itf);
                }
                public void visitTypeInsn(int op, String type) {
                  if (op == Opcodes.NEW && type.equals("java/util/Random")) { super.visitTypeInsn(op, "zshim/DetRandom"); return; }
                  super.visitTypeInsn(op, type);
                }
              };
            }
          }, 0);
          b = cw.toByteArray();
        }
        out.putNextEntry(new ZipEntry(e.getName())); out.write(b); out.closeEntry();
      }
    }
  }
}
