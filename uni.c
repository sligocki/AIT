// Binary Lambda Calculus universal machine heavily based on Ben Lynn's
// ION machine at https://crypto.stanford.edu/~blynn/compiler/ION.html
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

typedef uint32_t u32;
enum { MINMEMSZ = 1<<20, MAXMEMSZ = 1<<29 };
u32 memsize, *mem, *gcmem, *sp, *spTop, hp, qOpt, dbgGC, dbgSTP;
void die(char *s) { fprintf(stderr, "error: %s\n", s); exit(1); }
static inline u32 isComb(u32 n) { return n < 128; }
static inline u32 app(u32 f, u32 x) { mem[hp] = f; mem[hp+1] = x; return (hp+=2)-2; }

u32 nbits, inbits, mode;
u32 getbit() {
  if (!nbits) { nbits = mode; inbits = getchar(); } else nbits--;
  return (inbits>>nbits) & 1;
}
// switch(f) and switch(mem[f]) turn out to be WAY slower
u32 clapp(u32 f, u32 a) {
  return f=='K' && a=='I' ? 'F'
       : f=='B' && a=='K' ? 'D'
       : f=='C' && a=='I' ? 'T'
       : mem[f]=='B' && a=='I'? mem[f+1]
       : mem[f]=='R' && a=='I'? app('T',mem[f+1])
       : mem[f]=='B' && mem[f+1]=='C' &&  a=='T'? ':'
       : mem[f]=='S' && mem[f+1]=='I' &&  a=='I'? 'M'
       : !qOpt ? app(f, a)
       : f=='F' ? 'I'
       : f=='S' && a=='K' ? 'F'
       : f=='B' && a=='I' ? 'I'
       : f=='C' && a=='C' ? 'R'
       : mem[f]=='B' && mem[f+1]=='S' &&  a=='K'? 'B'
       : mem[f]=='B' && mem[mem[f+1]]=='S' &&  a=='K'? app('C',mem[mem[f+1]+1])
       : mem[a]=='K' && f=='S'? app('B',mem[a+1])
       : mem[f]=='R' && mem[f+1]=='I' &&  a=='B'? 'I'
       : app(f, a);
}
u32 parseBCL() {
  if (!getbit()) return "KS"[getbit()];
  u32 f = parseBCL(), a = parseBCL();
  return clapp(f, a);
}
u32 parseBLC() {
  u32 x;
  if (getbit()) {
    for (x=0; getbit(); x++) { }
    return app('V', x);
  }
  u32 isApp = getbit();
  x = parseBLC();
  return isApp ? app(x, parseBLC()) : app('L', x);
}

// does CL term have an occurance of Var 0 ?
u32 hasVar0(u32 cl) {
  if (isComb(cl)) return 0;
  u32 f = mem[cl], a = mem[cl+1];
  return f == 'V' ? a == 0 : hasVar0(f) || hasVar0(a);
}
// decrease variable depth
u32 drip(u32 cl) {
  if (isComb(cl)) return cl;
  u32 f = mem[cl], a = mem[cl+1];
  return f == 'V' ? app('V', a-1) : app(drip(f),drip(a));
}
// simple bracket abstraction
u32 abstract(u32 cl) {
  if (isComb(cl)) return clapp('K',cl);
  u32 f = mem[cl], a = mem[cl+1];
  if (f == 'V') return a ? app('K',app('V', a-1)) : 'I';
  switch (2 * hasVar0(f) + hasVar0(a)) {
    case 0: return clapp('K', drip(cl));
    case 1: f=drip(f);
            return mem[a]=='V' ? f
                 : clapp(clapp('B',f), abstract(a));
    case 2: return clapp(clapp('C',abstract(f)), drip(a));
    case 3: return clapp(clapp('S',abstract(f)), abstract(a));
  }
  return 0;
}

// if DB term has all occurances of Var n doubled, return undoubled version, else return 0
u32 unDoubleVar(u32 n, u32 db) {
  u32 udf, f = mem[db];
  if (f == 'V') return db;
  u32 uda, a = mem[db+1];
  if (f == 'L') return (uda = unDoubleVar(n+1,a)) ? app('L', uda) : 0;
  u32 qf = mem[f]=='V' && mem[f+1]==n;
  u32 qa = mem[a]=='V' && mem[a+1]==n;
  if (qf && qa) return app('V',n);
  if (qf || qa) return 0;
  return (udf = unDoubleVar(n,f)) && (uda = unDoubleVar(n,a)) ? app(udf,uda) : 0;
}
// recognize recursive functions by (\x.x x) (\x. f (x x)) template
u32 recursive(u32 f, u32 a) {
  return f=='M' && mem[a]=='L' && mem[f=mem[a+1]]!='V' && (f=unDoubleVar(0,f)) ? app('L',f) : 0;
}

// convert de-bruijn lambda term to combinatory logic term
u32 toCL(u32 db) {
  u32 f = mem[db];
  if (f == 'V') return db;
  u32 a = mem[db+1];
  if (f == 'L') return abstract(toCL(a));
  u32 fCL = toCL(f), ra = recursive(fCL,a);
  return ra ? app('Y',toCL(ra)) : clapp(fCL,toCL(a));
}

// Kiselyov bracket abstraction, explained in
// https://crypto.stanford.edu/~blynn/lambda/kiselyov.html
// storing list of booleans bools as foldr (\bool n-> app(n,bool?1:0)) 0 bools
u32 combineK(u32 n1, u32 d1, u32 n2, u32 d2) {
  if (n1==0)
    return n2==0 ? clapp(d1,d2)
     : mem[n2+1] ? combineK(0,clapp('B',d1), mem[n2],d2)
     :             combineK(0,          d1 , mem[n2],d2);
  else if (mem[n1+1])
    return n2==0 ? combineK(0,clapp('R',d2), mem[n1],d1)
     : mem[n2+1] ? combineK(mem[n1],combineK(0,'S', mem[n1],d1), mem[n2],d2)
     :             combineK(mem[n1],combineK(0,'C', mem[n1],d1), mem[n2],d2);
  else
    return n2==0 ? combineK(mem[n1],d1, 0,d2)
     : mem[n2+1] ? (!mem[n2] && mem[n2+1] && d2=='I' ? d1 // eta optimization not handled by clapp
                 : combineK(mem[n1],combineK(0,'B', mem[n1],d1), mem[n2],d2))
         :         combineK(mem[n1],d1, mem[n2],d2);
}
u32 zip(u32 nf, u32 na) {
  return !nf ? na : !na ? nf : app(zip(mem[nf],mem[na]),mem[nf+1]|mem[na+1]);
}
u32 convertK(u32 db, u32 *pn) {
  u32 nf, cf, na, ca, f = mem[db], a = mem[db+1];
  if (f == 'V') {
    for (nf = app(0,1); a--; ) nf = app(nf,0);
    *pn = nf;
    return 'I';
  }
  if (f == 'L') {
    ca = convertK(a, &na);
    if (na==0) { *pn = 0; return clapp('K', ca); }
    else { *pn = mem[na]; return mem[na+1] ? ca : combineK(0,'K', *pn,ca); }
  }
  cf = convertK(f, &nf);
  if (!nf && (ca = recursive(cf,a))) { cf = 'Y'; a = ca; }
  ca = convertK(a, &na);
  *pn = zip(nf, na);
  return combineK(nf,cf, na,ca);
}
u32 toCLK(u32 db) {
  u32 n, cl = convertK(db,&n);
  if (n) die("program not a closed term");
  return cl;
}

u32 evac(u32 n) {
  if (isComb(n)) return n;
  u32 x = mem[n];
  u32 y = mem[x];
  while (y == 'T') {
    mem[n] = y = mem[n+1];
    mem[n+1] = mem[x+1];
    y = mem[x = y];
  }
  if (y == 'K') {
    mem[n+1] = mem[x+1];
    x = mem[n] = 'I';
  }
  y = mem[n + 1];
  if (!x) return y;
  if (x == 'I') {
    mem[n] = 0;
    return mem[n+1] = evac(y);
  }
  gcmem[hp] = x; gcmem[hp+1] = y;
    mem[ n] = 0;   mem[ n+1] = hp;
  return (hp += 2) - 2;
}

u32 *reheap(u32* m, u32 size) {
  m = realloc(m, (size_t)size * sizeof(u32));
  if (!m) die("realloc failed");
  memset(m, 0, 128); // allow mem[x]=='C' test without !isComb(x)
  return m;
}
u32 steps, nGC, qDblMem;
void putch(u32 c) { putchar(c); fflush(stdout); }
void stats() { fprintf(stderr, "\nsteps %u heap %u stack %td\n", steps, hp, spTop - sp); }
static inline u32 arg(u32 n) { return mem[sp [n] + 1]; }
static inline u32 apparg(u32 i, u32 j) { return app(arg(i), arg(j)); }
static inline void lazy(u32 delta, u32 f, u32 x) {
  sp += delta;
  u32 *p = mem + sp[1];
  p[0] = f; p[1] = x;
}
void gc() {
  nGC++;
  if (dbgGC) { stats(); fprintf(stderr, "memsize %u GC %u -> ", memsize, hp-128); }
  if (qDblMem) gcmem = reheap(gcmem, memsize *= 2);
  sp = gcmem + memsize-1;
  u32 di = hp = 128;
  for (*sp = evac(*spTop); di < hp; di++)
    gcmem[di] = evac(gcmem[di]);
  if (dbgGC) fprintf(stderr, "%u\n", hp-128);
  spTop = sp;
  u32 *old = mem;
  mem = gcmem;
  gcmem = old;
  if (qDblMem) gcmem = reheap(gcmem, memsize);
  qDblMem = hp >= memsize/2 && memsize < MAXMEMSZ;
}

void run(u32 x) {
  *(sp = spTop = mem + memsize - 1) = x;
  char outbits = 0;
  for (steps = nGC = 0; ; steps++) {
    if (dbgSTP && !(steps & (1<<28)-1)) stats();
    if (mem + hp > sp - 8) { gc(); x = *sp; }
    for (; !isComb(x); x = mem[x]) *sp-- = x;
    switch (x) {
      case 'M': x = arg(1); break;
      case 'I': x = mem[*++sp + 1]; break;
      case 'Y': lazy(0, x = arg(1), app('Y',arg(1))); break;
      case '+': lazy(0, x = app(arg(1),'0'), '1'); break; // output bits
      case '>': lazy(0, x = app(arg(1),'+'), '!'); break; // output bytes
      case 'K': lazy(1, x = 'I', arg(1)); break;
      case 'F': lazy(1, x = 'I', arg(2)); break;
      case 'T': lazy(1, x = arg(2), arg(1)); break;
      case 'D': lazy(2, x = arg(1), arg(2)); break;
      case 'B': lazy(2, x = arg(1), apparg(2,3)); break;
      case 'C': lazy(2, x = apparg(1,3), arg(2)); break;
      case 'R': lazy(2, x = apparg(2,3), arg(1)); break;
      case ':': lazy(2, x = apparg(3,1), arg(2)); break;
      case 'S': lazy(2, x = apparg(1,3), apparg(2,3)); break;
      case '0': case '1': if (mode) outbits = outbits<<1 | (x&1);
                          else putch(x);                  // output bit
                lazy(0, x = arg(1), '+'); break;
      case '!': putch(outbits);                           // output byte
                lazy(0, x = arg(1), '>'); break;
      case '-': getbit(); nbits++;                        // input
                if (inbits == EOF) { lazy(0, x = 'K', 'I'); break; }
                if (mode) {
                  for (x='F'; nbits; nbits--,inbits>>=1)
                    x = app(app(':', "KF"[inbits&1]), x);
                } else x = "KF"[getbit()];
                lazy(0, x = app(':', x), app('-','?')); break;
      case '.': return;                                   // end-of-output
      default: die("unknown combinator");
    }
  }
}

void show(u32 n) {
  if (!isComb(n)) {
    u32 f = mem[n], a = mem[n+1];
    if (f == 'V') putch('0'+a);
    else if (f == 'L') { putch('\\'); show(a); }
    else { putch('`'); show(f); show(a); }
  } else putch(n);
}
void shownl(u32 n) { show(n); putchar('\n'); }
int main(int argc, char **argv) {
  u32 db, dbgProg, bcl, plainBA;
  dbgGC = dbgProg = dbgSTP = qOpt = qDblMem = bcl = plainBA = nbits = db = 0;
  mode = 7;                         // default byte mode
  int opt;
  while ((opt = getopt(argc, argv, "bcgkpqs")) != -1) {
    switch (opt) {
      case 'b': mode = 0; break;    // bit mode
      case 'c': bcl = 1; break;     // binary combinatory logic
      case 'g': dbgGC = 1; break;   // show garbage collection stats
      case 'k': plainBA = 1; break; // use plain bracket abstraction, no Kiselyov
      case 'p': dbgProg = 1; break; // print parsed program
      case 'q': qOpt = 1; break;    // questionable clapp optimizations
      case 's': dbgSTP = 1; break;  // show steps every 2^28
    }
  }
  mem = reheap(NULL, memsize = MINMEMSZ); gcmem = reheap(NULL, memsize);
  hp = 128;
  u32 cl = bcl ? parseBCL() : (plainBA ? toCL : toCLK)(db = parseBLC());
  if (dbgProg) { if (db) shownl(db); shownl(cl); }
  nbits = 0;            // skip remaining bits in last program byte
  clock_t start = clock();
  run(app(app(app(cl, app('-','?')), mode ? '>' : '+'),'.'));
  clock_t end = clock();
  u32 ms = (end - start) * 1000 / CLOCKS_PER_SEC;
  fprintf(stderr, "\nsteps %u time %ums steps/s %uM #GC %u HP %u\n",
    steps, ms, ms ? steps/ms/1000 : 666, nGC, hp);
  return 0;
}
