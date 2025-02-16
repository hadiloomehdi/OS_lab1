// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

#define ShiftKey(x) ((x) + ' ') // Shift-x

static void consputc(int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  }
  // else if(c == ShiftKey('[')) {
  //   pos = pos - (pos % 80) + 2;
  // } ????
  else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  // crt[pos] = ' ' | 0x0700;
  // crt[pos] =  0x700;
}
// backCursor
static void
changeCursor (int changeValue)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  pos = pos + changeValue;

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");
  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }
  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  // crt[pos] = ' ' | 0x0700;
  // crt[pos] = 0x0700;
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  cgaputc(c);
}

#define INPUT_BUF 128
struct {
  char buf[INPUT_BUF];
  uint read;  // Read index
  uint write;  // Write index
  uint edit;  // Edit index
  uint last;  // Last index
  uint size;  // 
  // uint shift; // Shift flag
} input;

#define C(x)  ((x)-'@')  // Control-x

int nextIndex(int ind)
{
  return ind + 1;
  // return (ind + 1) % INPUT_BUF;
}

int prevIndex(int ind)
{
  return ind - 1;
  // return (ind - 1 + INPUT_BUF) %  INPUT_BUF;
}

int getIndex (int ind) {
  return ind % INPUT_BUF;
}


void writeBuffer (uint startIndex, uint endIndex) {
  uint currentIndex = startIndex;
  while (currentIndex != endIndex) {
    cgaputc(input.buf[getIndex(currentIndex)]);
    currentIndex = nextIndex(currentIndex);
  }
  changeCursor(-(endIndex - startIndex));
}

void moveCursorToStartLine () {
  changeCursor(-(input.edit - input.write));
  input.edit = input.write;
}

void nextLine () {
    changeCursor(input.last - input.edit);
    input.buf[getIndex(input.last)] = '\n';
    input.last = nextIndex(input.last);
    // cgaputc('\n');
    consputc('\n');
    input.edit = input.last;
    input.write = input.last;
    input.size = 0;
}

void typeCharecter (char c) {
  if (c == '\n') {
    nextLine();
    return;
  }
  input.size++;
  input.last = nextIndex(input.last);
  uint currentIndex = input.last;
  while (currentIndex != input.edit) {
    input.buf[getIndex(currentIndex)] = input.buf[getIndex(prevIndex(currentIndex))];
    currentIndex = prevIndex(currentIndex);
  }
  input.buf[getIndex(input.edit)] = c;
  writeBuffer(input.edit, input.last);
  input.edit = nextIndex(input.edit);
  changeCursor(1);
}

void goEndLine () {
  changeCursor (input.last - input.edit);
  input.edit = input.last;
}

void eraseCharacter () {
  input.edit = prevIndex(input.edit);
  uint currentIndex = input.edit;
  changeCursor(-1);
  while (currentIndex + 1 != input.last) {
    input.buf[getIndex(currentIndex)] = input.buf[getIndex(nextIndex(currentIndex))];
    cgaputc(input.buf[currentIndex]);
    currentIndex = nextIndex(currentIndex);
  }
  cgaputc(' ');
  cgaputc(BACKSPACE);
  input.last--;
  changeCursor(-(input.last - input.edit));
  input.size--;
}

void eraseAll () {
  changeCursor(input.last - input.edit);
  input.edit = input.last;
  while (input.write != input.last)
    eraseCharacter();
  input.size = 0;
}



void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'): case C('C'): // Kill line.
      eraseAll();
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input.edit != input.write) {
        eraseCharacter();
        // else {
        //   input.edit--;
        //   input.last--;
        //   conspu tc(+ 1BACKSPACE);
        // }
      }
      break;
    case ShiftKey('['):
        moveCursorToStartLine ();
      break;
    case ShiftKey(']'):
        goEndLine();
      break;
    default:
      if(c != 0 && (input.last - input.read < INPUT_BUF)) {
        c = (c == '\r') ? '\n' : c;
        typeCharecter(c);
        if(c == '\n' || c == C('D') || input.last == input.read){
          input.size = 0;
          // changeCursor(input.last - input.edit);
          wakeup(&input.read);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input.read == input.write){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.read, &cons.lock);
    }
    
    c = input.buf[input.read++ % INPUT_BUF];
    // input.read = nextIndex(input.read);
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.read = prevIndex(input.read);
      }
      break;
    }
    *dst++ = c;
    // n = prevIndex(n);
    --n;
    if(c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}
