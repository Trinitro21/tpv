#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>

//settings
int device=-1;//which device to read from, /dev/input/eventX if libevdev
int widthmult=16;//width multiplier
int fixedwidth=1;//fixed width
int width=60;//width if fixed
int shape=0;//0=circle, 1=rectangle
int shapeaftertap=0;//should it keep drawing the shape after the touch moves a certain amount
int tapthreshold=30;//how much should the touch move to no longer be considered a tap
int trail=1;//is there a line trail
int trailduringtap=0;//should the trail be shown before the tap threshold gets passed
int trailstartdistance=0;//how far from the current touch should the trail start
int traillength=8;//how many points behind the touch should the trail include
int traildispersion=1;//how many frames away should those points be
int trailisshape=0;//line or shape
int trailshape=0;//0=circle, 1=rectangle
int fps=60;//framerate so it aligns to your refresh rate hopefully
int outputmethod=1;//0=none, 1=draw on root window, 2=draw on composite overlay window
int inputmethod=1;//0=libevdev, 1=XInput2
char *edgeswipes[8];//top, bottom, left, right
int edgeswipethreshold=1;//how many units away from the edge of the screen does the touch have to start at to be considered an edge swipe

FILE * config;
char *configpath="/.config/tpv";

struct libevdev *dev=NULL;
int i,j;//used as the iterator in for loops
int f;//file for libevdev
int ev=1;//holds the return value for getting the event
int tt;//how many touches are available
char filepath[20];//holds the filepath
struct input_event e;

XExposeEvent xev;

int xiopcode;
XEvent xevent;
XIDeviceEvent *xiev;
int *currentd,tindex;

Display *disp;
int screen;
Window root,window;
unsigned int screendepth;
Visual *screenvisual;

XGCValues gcval;
GC gc;

int sw;//screen dimensions
int sh;

int tw;//dimensions of what libevdev reports
int th;

int sx,sy,ex,ey;//things of redraw area

void addtobound(int x,int y,int x2,int y2){
	if(sx==-1||sx>x)sx=x-1;
	if(sx==-1||sx>x2)sx=x2-1;
	if(sy==-1||sy>y)sy=y-1;
	if(sy==-1||sy>y2)sy=y2-1;
	if(ex==-1||ex<x)ex=x+1;
	if(ex==-1||ex<x2)ex=x2+1;
	if(ey==-1||ey<y)ey=y+1;
	if(ey==-1||ey<y2)ey=y2+1;
}

void backgroundshell(char string[]){
	if(string!=NULL && strcmp(string,"\n") && strcmp(string,"")){//if the command isn't blank
		pid_t pid=fork();//run in background so nonblocking
		if(pid<0){
			fprintf(stderr,"There was an error forking a new process\n");
		}else if(pid==0){//is child
			system(string);//run the command
			_exit(0);//and go away we don't want you anymore
		}
	}
}

int touchnumfromdetail(int detail,int (*currentd),int (*d)[tt],int tt){
	int i;
	for(i=0;i<tt;i++)
		if(currentd[i]==detail)
			return i;//if the touch is already handled
	for(i=0;i<tt;i++)//but if it isn't find a new one
		if(d[0][i]==0){
			currentd[i]=detail;
			return i;
		}
	fprintf(stderr,"There was an error getting the touch index, ignoring\n");
	return -1;//oh no what
}

int stringtoint(char string[]){
	unsigned int result=0,i;
	for(i=0;i<strlen(string);i++){
		if(string[i]==10)continue;
		if(string[i]<'0' || string[i]>'9'){
			printf("Invalid character '%s' (%d) in '%s', expected number\n",&string[i],string[i],string);
			exit(1);
		}
		result=result*10+string[i]-'0';
	}
	return result;
}

void stringtoedgeswipes(char string[],int index){
	edgeswipes[index]=malloc(strlen(string)+1);
	strcpy(edgeswipes[index],string);
}

void parseconfig(FILE * conf){
	char configbuff[50],*name,*val,*buff;
	while(fgets(configbuff,50,conf)!=NULL){
		buff=configbuff;
		if(!(name=strsep(&buff," ")))
			continue;
		if(!(val=strsep(&buff,"\n")))
			continue;
		if(strcmp("device",name)==0)
			device=stringtoint(val);
		if(strcmp("widthmult",name)==0)
			widthmult=stringtoint(val);
		if(strcmp("width",name)==0)
			width=stringtoint(val);
		if(strcmp("fixedwidth",name)==0)
			fixedwidth=stringtoint(val);
		if(strcmp("shape",name)==0){
			if(strcmp("circle",val)==0)
				shape=0;
			else if(strcmp("square",val)==0)
				shape=1;
		}
		if(strcmp("shapeaftertap",name)==0)
			shapeaftertap=stringtoint(val);
		if(strcmp("tapthreshold",name)==0)
			tapthreshold=stringtoint(val);
		if(strcmp("trail",name)==0)
			trail=stringtoint(val);
		if(strcmp("trailduringtap",name)==0)
			trailduringtap=stringtoint(val);
		if(strcmp("trailstartdistance",name)==0)
			trailstartdistance=stringtoint(val);
		if(strcmp("traillength",name)==0)
			traillength=stringtoint(val);
		if(strcmp("traildispersion",name)==0)
			traildispersion=stringtoint(val);
		if(strcmp("trailisshape",name)==0)
			trailisshape=stringtoint(val);
		if(strcmp("trailshape",name)==0){
			if(strcmp("circle",val)==0)
				trailshape=0;
			else if(strcmp("square",val)==0)
				trailshape=1;
		}
		if(strcmp("fps",name)==0)
			fps=stringtoint(val);
		if(strcmp("inputmethod",name)==0){
			if(strcmp("libevdev",val)==0)
				inputmethod=0;
			else if(strcmp("xinput2",val)==0)
				inputmethod=1;
		}
		if(strcmp("outputmethod",name)==0){
			if(strcmp("none",val)==0)
				outputmethod=0;
			else if(strcmp("root",val)==0)
				outputmethod=1;
			else if(strcmp("compositeoverlay",val)==0)
				outputmethod=2;
		}
		if(strcmp("edgeswipethreshold",name)==0)
			edgeswipethreshold=stringtoint(val);
		if(strcmp("edgetop",name)==0)
			stringtoedgeswipes(val,0);
		if(strcmp("edgebottom",name)==0)
			stringtoedgeswipes(val,1);
		if(strcmp("edgeleft",name)==0)
			stringtoedgeswipes(val,2);
		if(strcmp("edgeright",name)==0)
			stringtoedgeswipes(val,3);
		if(strcmp("edgetopend",name)==0)
			stringtoedgeswipes(val,4);
		if(strcmp("edgebottomend",name)==0)
			stringtoedgeswipes(val,5);
		if(strcmp("edgeleftend",name)==0)
			stringtoedgeswipes(val,6);
		if(strcmp("edgerightend",name)==0)
			stringtoedgeswipes(val,7);
	}
}

int main(int argc, char **argv){
	//aaah
	char *confpath=malloc(strlen(getenv("HOME"))+strlen(configpath)+1);
	sprintf(confpath,"%s%s",getenv("HOME"),configpath);
	config=fopen(confpath,"r");
	if(config!=NULL){//parse config
		parseconfig(config);
		fclose(config);
	}
	//set up x stuff
	disp=XOpenDisplay(NULL);
	if(disp==NULL){
		fprintf(stderr,"Cannot open display\n");
		exit(1);
	}
	screen=DefaultScreen(disp);
	
	sw=DisplayWidth(disp,screen);//get the screen dimensions
	sh=DisplayHeight(disp,screen);
	
	screendepth=DefaultDepth(disp,screen);
	screenvisual=DefaultVisual(disp,screen);
	
	root=RootWindow(disp,screen);
	if(outputmethod==1)
		window=root;
	else if(outputmethod==2){
		if(!XQueryExtension(disp,"Composite",&i,&j,&ev)){//i, j, ev because they're unused ints at the moment
			fprintf(stderr,"XComposite extension not available");
			exit(1);
		}
		window=XCompositeGetOverlayWindow(disp,root);
	}
	
	if(inputmethod==0){//use libevdev
		//get the filepath from $1
		sprintf(filepath,"/dev/input/event%d",device);
		f=open(filepath,O_RDONLY|O_NONBLOCK);
		if(f<0){
			fprintf(stderr,"Could not open %s, probably a permissions issue\n",filepath);
			return 1;
		}
		//init evdev
		ev=libevdev_new_from_fd(f,&dev);
		if(ev<0){
			fprintf(stderr,"Libevdev failed to initialize on the input stream\n");
			return 1;
		}
		//get max dimensions
		tw=libevdev_get_abs_maximum(dev,53);
		th=libevdev_get_abs_maximum(dev,54);
		tt=libevdev_get_num_slots(dev);
	}else if(inputmethod==1){//use xinput2
		currentd=calloc(tt,sizeof(int));
		//memset(currentd,0,tt*sizeof(int));//just uh fit that in there
		int xia,xib;
		if(!XQueryExtension(disp,"XInputExtension",&xiopcode,&xia,&xib)){//j, ev because they're unused ints at the moment
			fprintf(stderr,"XInput extension not available\n");
			exit(1);
		}
		int maj=2,min=2;
		ev=XIQueryVersion(disp,&maj,&min);
		if(ev!=Success){
			fprintf(stderr,"XInput is too old to support touch\n");
			exit(1);
		}
		//why is this so hard to find info on
		//i just wanna passive grab all touches just please
		XIEventMask xiem[2];
		XIEventMask *mask;
		mask=&xiem[0];
		mask->deviceid=(device==-1?XIAllMasterDevices:device);
		mask->mask_len=XIMaskLen(XI_LASTEVENT);
		mask->mask=calloc(mask->mask_len,sizeof(char));
		XISetMask(mask->mask,XI_RawTouchBegin);
		XISetMask(mask->mask,XI_RawTouchUpdate);
		XISetMask(mask->mask,XI_RawTouchEnd);
		XISelectEvents(disp,root,&xiem[0],1);
		XSync(disp,False);
		tt=10;//just gonna uh do that
		//i don't feel like learning how to scan devices and pick out the number of touches fron stuff
		//you're not going to have more than 10 touches typically anyways ok
		//and even if i did learn, there's an option for unlimited touches
	}
	
	//init touch arrays
	int tx[traillength*traildispersion+2][tt];//x
	int ty[traillength*traildispersion+2][tt];//y
	int d[traillength*traildispersion+2][tt];//is touched, needs to be initialized to zeroes
	memset(d,0,(traillength*traildispersion+2)*tt*sizeof(int));
	int r[traillength*traildispersion+2][tt];//width
	//traillength*traildispersion+2 because the first slot stores the current touch, there are traillength*traildispersion more that are part of the trail, and the last one holds the initial conditions of the touch
	//on the last index, r will be used as a switch for tapthreshold
	
	//set up expose event
	xev.type=Expose;
	xev.serial=0;
	xev.send_event=True;
	xev.display=disp;
	xev.window=window;
	xev.count=0;
	
	//init graphics context
	if(outputmethod!=0){
		gcval.foreground=XWhitePixel(disp,0);
		gcval.background=XBlackPixel(disp,0);
		gcval.function=GXxor;
		gcval.plane_mask=gcval.background^gcval.foreground;
		gcval.subwindow_mode=IncludeInferiors;
		gc=XCreateGC(disp,window,GCFunction|GCForeground|GCBackground|GCSubwindowMode,&gcval);
	}
	
	while(1){
		waitpid((pid_t)-1,&ev,WNOHANG);//stupid zombies
		if(fps!=0)
			usleep(1000000/fps);
		//clear the drawn areas in case it didn't already repaint
		if(ex-sx>0 && ey-sy>0){
			xev.x=sx;
			xev.y=sy;
			xev.width=ex-sx;
			xev.height=ey-sy;
			XSendEvent(disp,window,True,ExposureMask,(XEvent*)&xev);
		}
		if(inputmethod==0){
			do{//make sure libevdev is completely updated on the device events
				ev=libevdev_next_event(dev,LIBEVDEV_READ_FLAG_NORMAL,&e);
			}while(ev==0);
		}else if(inputmethod==1){
			while(XPending(disp)){
				XNextEvent(disp,&xevent);
				if(xevent.xcookie.type==GenericEvent && xevent.xcookie.extension==xiopcode && XGetEventData(disp,&xevent.xcookie)){
					xiev=xevent.xcookie.data;
					switch(xevent.xcookie.evtype){
						case XI_TouchBegin:
							if((tindex=touchnumfromdetail(xiev->detail,currentd,d,tt))<0)
								break;
							d[0][tindex]=1;
							r[0][tindex]=width;
							tx[0][tindex]=xiev->event_x;
							ty[0][tindex]=xiev->event_y;
							break;
						case XI_RawTouchBegin:
							if((tindex=touchnumfromdetail(xiev->detail,currentd,d,tt))<0)
								break;
							d[0][tindex]=1;
							r[0][tindex]=width;
							tx[0][tindex]=xiev->event_x*sw/65535;
							ty[0][tindex]=xiev->event_y*sh/65535;
							break;
						case XI_TouchUpdate:
							if((tindex=touchnumfromdetail(xiev->detail,currentd,d,tt))<0)
								break;
							d[0][tindex]=1;
							r[0][tindex]=width;
							tx[0][tindex]=xiev->event_x;
							ty[0][tindex]=xiev->event_y;
							break;
						case XI_RawTouchUpdate:
							if((tindex=touchnumfromdetail(xiev->detail,currentd,d,tt))<0)
								break;
							d[0][tindex]=1;
							r[0][tindex]=width;
							tx[0][tindex]=xiev->event_x*sw/65535;
							ty[0][tindex]=xiev->event_y*sh/65535;
							break;
						case XI_TouchEnd:
						case XI_RawTouchEnd:
							if((tindex=touchnumfromdetail(xiev->detail,currentd,d,tt))<0)
								break;
							d[0][tindex]=0;
							break;
					}
				}
				XFreeEventData(disp,&xevent.xcookie);
			}
		}
		//draw the things
		sx=-1;sy=-1;ex=-1;ey=-1;//init the bounding rect
		for(i=0;i<tt;i++){
			//get the values
			if(inputmethod==0){
				tx[0][i]=libevdev_get_slot_value(dev,i,53)*sw/tw;//get touch coordinatess
				ty[0][i]=libevdev_get_slot_value(dev,i,54)*sh/th;
				d[0][i]=(libevdev_get_slot_value(dev,i,57)!=-1);//is touch actually
				if(fixedwidth==0){//if fixed width is off, adjust width
					r[0][i]=libevdev_get_slot_value(dev,i,48)*widthmult;//what width is
				}else{
					r[0][i]=width;
				}
			}
			if(d[0][i] && !d[1][i]){
				tx[traillength*traildispersion+1][i]=tx[0][i];
				ty[traillength*traildispersion+1][i]=ty[0][i];
				r[traillength*traildispersion+1][i]=1;//tap threshold flag
				d[traillength*traildispersion+1][i]=d[0][i];
				//edge swipes
				if(ty[traillength*traildispersion+1][i]<=edgeswipethreshold)
					backgroundshell(edgeswipes[0]);
				if(ty[traillength*traildispersion+1][i]>=sh-1-edgeswipethreshold)
					backgroundshell(edgeswipes[1]);
				if(tx[traillength*traildispersion+1][i]<=edgeswipethreshold)
					backgroundshell(edgeswipes[2]);
				if(tx[traillength*traildispersion+1][i]>=sw-1-edgeswipethreshold)
					backgroundshell(edgeswipes[3]);
			}
			if(!d[0][i] && d[1][i]){//end edge swipe gestres
				if(ty[traillength*traildispersion+1][i]<=edgeswipethreshold)
					backgroundshell(edgeswipes[4]);
				if(ty[traillength*traildispersion+1][i]>=sh-1-edgeswipethreshold)
					backgroundshell(edgeswipes[5]);
				if(tx[traillength*traildispersion+1][i]<=edgeswipethreshold)
					backgroundshell(edgeswipes[6]);
				if(tx[traillength*traildispersion+1][i]>=sw-1-edgeswipethreshold)
					backgroundshell(edgeswipes[7]);
			}
			if(d[0][i]){
				if(sqrt((double)((tx[0][i]-tx[traillength*traildispersion+1][i])*(tx[0][i]-tx[traillength*traildispersion+1][i])+(ty[0][i]-ty[traillength*traildispersion+1][i])*(ty[0][i]-ty[traillength*traildispersion+1][i])))>=(double)tapthreshold)//check tap threshold
					r[traillength*traildispersion+1][i]=0;
				if(outputmethod!=0 && (shapeaftertap || r[traillength*traildispersion+1][i])){//should it draw the current touch
					addtobound(tx[0][i]-r[0][i]/2,ty[0][i]-r[0][i]/2,tx[0][i]+r[0][i]/2,ty[0][i]+r[0][i]/2);
					if(shape==0)
						XDrawArc(disp,window,gc,tx[0][i]-r[0][i]/2,ty[0][i]-r[0][i]/2,r[0][i],r[0][i],0,360*64);
					if(shape==1)
						XDrawRectangle(disp,window,gc,tx[0][i]-r[0][i]/2,ty[0][i]-r[0][i]/2,r[0][i],r[0][i]);
				}
				if(outputmethod!=0 && trail && (trailduringtap || !r[traillength*traildispersion+1][i])){//should it draw the trail
					for(j=traildispersion;j<traillength*traildispersion+1;j+=traildispersion){
						if(!d[j][i])
							break;
						if(trailstartdistance>0)
							if(sqrt((double)((tx[j][i]-tx[0][i])*(tx[j][i]-tx[0][i])+(ty[j][i]-ty[0][i])*(ty[j][i]-ty[0][i])))<(double)trailstartdistance)//is it outside the start distance yet
								continue;
						if(!trailisshape){//draw a line
							addtobound(tx[j-traildispersion][i],ty[j-traildispersion][i],tx[j][i],ty[j][i]);
							XDrawLine(disp,window,gc,tx[j-traildispersion][i],ty[j-traildispersion][i],tx[j][i],ty[j][i]);
						}else{//draw a shape
							addtobound(tx[j][i]-r[j][i]/2,ty[j][i]-r[j][i]/2,tx[j][i]+r[j][i]/2,ty[j][i]+r[j][i]/2);
							if(trailshape==0)
								XDrawArc(disp,window,gc,tx[j][i]-r[j][i]/2,ty[j][i]-r[j][i]/2,r[j][i],r[j][i],0,360*64);
							if(trailshape==1)
								XDrawRectangle(disp,window,gc,tx[j][i]-r[j][i]/2,ty[j][i]-r[j][i]/2,r[j][i],r[j][i]);
						}
					}
				}
			}
			for(j=traillength*traildispersion-1;j>=0;j--){//advance the buffer
				tx[j+1][i]=tx[j][i];
				ty[j+1][i]=ty[j][i];
				r[j+1][i]=r[j][i];
				d[j+1][i]=d[j][i];
			}
		}
		XFlush(disp);
	}
	
	XCloseDisplay(disp);
	return 0;
}
