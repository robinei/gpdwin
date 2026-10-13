/* glb.c: surfaceless EGL/GLES2 microbenchmark for crocus submission cost (no window).
 * Renders F frames into a 256x256 FBO; each frame = D draws (each binding a different texture
 * and VBO so that every draw adds relocations); glFlush() after every B draws forces a batch submit.
 * Prints wall/utime/stime per frame and per flush (getrusage), so the CPU cost of submissions
 * can be compared across batch sizes. Run under `strace -c -e ioctl` for kernel-side ioctl time.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static double now(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return ts.tv_sec+ts.tv_nsec*1e-9;}
static double ru(struct rusage*r,int s){struct timeval tv=s?r->ru_stime:r->ru_utime;return tv.tv_sec+tv.tv_usec*1e-6;}
static GLuint sh(GLenum t,const char*s){GLuint h=glCreateShader(t);glShaderSource(h,1,&s,0);glCompileShader(h);GLint ok;glGetShaderiv(h,GL_COMPILE_STATUS,&ok);if(!ok){char l[1024];glGetShaderInfoLog(h,1024,0,l);fprintf(stderr,"shader: %s\n",l);exit(1);}return h;}

int main(int argc,char**argv){
  int F=argc>1?atoi(argv[1]):300;   /* frames */
  int D=argc>2?atoi(argv[2]):64;    /* draws per frame */
  int B=argc>3?atoi(argv[3]):64;    /* draws per flush (batch) */
  int T=argc>4?atoi(argv[4]):32;    /* distinct textures/VBOs cycled through */
  PFNEGLGETPLATFORMDISPLAYEXTPROC getDpy=(void*)eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy=getDpy?getDpy(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,0):EGL_NO_DISPLAY;
  if(dpy==EGL_NO_DISPLAY){fprintf(stderr,"no surfaceless display\n");return 1;}
  if(!eglInitialize(dpy,0,0)){fprintf(stderr,"eglInitialize failed\n");return 1;}
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint cfgattr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
  EGLConfig cfg;EGLint n=0;eglChooseConfig(dpy,cfgattr,&cfg,1,&n);
  EGLint ctxattr[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
  EGLContext ctx=eglCreateContext(dpy,n?cfg:EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,ctxattr);
  if(ctx==EGL_NO_CONTEXT){fprintf(stderr,"no context\n");return 1;}
  if(!eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx)){fprintf(stderr,"makecurrent failed\n");return 1;}
  fprintf(stderr,"GL_RENDERER: %s\n",glGetString(GL_RENDERER));
  GLuint fbo,rb;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8_OES,256,256);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){fprintf(stderr,"fbo incomplete\n");return 1;}
  glViewport(0,0,256,256);
  GLuint p=glCreateProgram();glAttachShader(p,sh(GL_VERTEX_SHADER,"attribute vec2 a;varying vec2 v;uniform vec2 o;void main(){v=a*0.5+0.5;gl_Position=vec4(a*0.05+o,0.0,1.0);}"));glAttachShader(p,sh(GL_FRAGMENT_SHADER,"precision mediump float;varying vec2 v;uniform sampler2D s;void main(){gl_FragColor=texture2D(s,v);}"));glLinkProgram(p);glUseProgram(p);
  GLint uo=glGetUniformLocation(p,"o");
  GLuint*tex=calloc(T,4),*vbo=calloc(T,4);glGenTextures(T,tex);glGenBuffers(T,vbo);
  static unsigned char px[16*16*4];for(int i=0;i<sizeof px;i++)px[i]=i;
  float tri[6]={-1,-1,1,-1,0,1};
  for(int i=0;i<T;i++){glBindTexture(GL_TEXTURE_2D,tex[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,px);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glBindBuffer(GL_ARRAY_BUFFER,vbo[i]);glBufferData(GL_ARRAY_BUFFER,sizeof tri,tri,GL_STATIC_DRAW);}
  glEnableVertexAttribArray(0);
  /* warm up */
  for(int f=0;f<10;f++){for(int d=0;d<D;d++){glBindTexture(GL_TEXTURE_2D,tex[d%T]);glBindBuffer(GL_ARRAY_BUFFER,vbo[d%T]);glVertexAttribPointer(0,2,GL_FLOAT,0,0,0);glUniform2f(uo,(d%8)/8.0f-0.5f,(d/8%8)/8.0f-0.5f);glDrawArrays(GL_TRIANGLES,0,3);if((d+1)%B==0)glFlush();}glFlush();}
  glFinish();
  struct rusage r0,r1;getrusage(RUSAGE_SELF,&r0);double w0=now();
  for(int f=0;f<F;f++){
    for(int d=0;d<D;d++){glBindTexture(GL_TEXTURE_2D,tex[d%T]);glBindBuffer(GL_ARRAY_BUFFER,vbo[d%T]);glVertexAttribPointer(0,2,GL_FLOAT,0,0,0);glUniform2f(uo,(d%8)/8.0f-0.5f,(d/8%8)/8.0f-0.5f);glDrawArrays(GL_TRIANGLES,0,3);if((d+1)%B==0)glFlush();}
    if(D%B)glFlush();
  }
  glFinish();
  double w1=now();getrusage(RUSAGE_SELF,&r1);
  int flushes=(D+B-1)/B;
  printf("F=%d D=%d B=%d T=%d | per frame: wall %.1f us, utime %.1f us, stime %.1f us | per flush(batch): wall %.1f us, utime %.1f us, stime %.1f us | per draw: wall %.2f us\n",F,D,B,T,(w1-w0)/F*1e6,(ru(&r1,0)-ru(&r0,0))/F*1e6,(ru(&r1,1)-ru(&r0,1))/F*1e6,(w1-w0)/F/flushes*1e6,(ru(&r1,0)-ru(&r0,0))/F/flushes*1e6,(ru(&r1,1)-ru(&r0,1))/F/flushes*1e6,(w1-w0)/F/D*1e6);
  return 0;
}
