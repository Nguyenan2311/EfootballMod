package zshim;
public class DetRandom extends java.util.Random { static long next = Long.getLong("zseed", 1000L); public DetRandom(){ super(next++); } }
