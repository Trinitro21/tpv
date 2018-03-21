#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XTest.h>
#include <X11/Xutil.h>

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
int hidemouse=1;//hides the mouse on touch
int mousedevice=-1;//how do i get at that mouse
int buttonlisten=1;//if xinput2 should the mouse buttons be listened to (if yes it will show mouse on the rightclickonhold)
int rightclickonhold=1;//should the program simulate a rightclick if you hold for long enough
int rightclicktime=1000;//how much time in milliseconds should the touch be held down for before a rightclick is triggered
int rightclickonend=1;//whether to wait until the touch has been let go or do the rightclick immediately
int rightclickontwofingertap=0;//if a rightclick should be triggered when the user taps with two fingers
int rightclickmethod=0;//0=xtest, 1=command
char *rightclickcommand;//command to execute for a right click
int shapechangeifrightclick=1;//if the shape of the touch visual should change if a rightclick will be triggered on touch lift
int fps=60;//framerate so it aligns to your refresh rate hopefully
int outputwindow=1;//0=none, 1=draw on root window, 2=draw on composite overlay window
int clearmethod=3;//0=none, 1=xexposeevent, 2=xcleararea, 3=xexposeevent and xcleararea
int inputmethod=1;//0=libevdev, 1=XInput2
char *edgeswipes[8];//top, bottom, left, right
int edgeswipethreshold=1;//how many units away from the edge of the screen does the touch have to start at to be considered an edge swipe
int edgeswipeextrapolate=1;//should the program extrapolate the touch at the frame before the first touch frame to determine if the touch is an edge swipe
int releasedecay=0;//how many frames should it take for the shape to shrink before it disappears after the touch is over
int mousedelay=2;//how many frames of exclusively mouse movement before the mouse is shown, used when the mouse flickers

FILE * config;
char *configpath="/.config/tpv";

struct libevdev *dev=NULL;
struct libevdev *devmouse=NULL;
int i,j;//used as the iterator in for loops

int xiopcode;
XEvent xevent;
XIDeviceEvent *xiev;
int *currentd,tindex;

int f;//file for libevdev
char filepath[30];//holds the filepath
int ev=1;//holds the return value for getting the event
int tt;//how many touches are available
int tlength;//size of touch array, (traillength*traildispersion+2)
struct input_event e;

XExposeEvent xev;
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

int **tx,**ty,**d,**r;//touch x, touch y, touch down, touch width

int **tmax;//for storing device max values in xinput2

//stores the timestamp of the initial touch
struct timespec *timestamps,tsbuff;
int es;//flags for edgeswipes so it can tell on the last frame of a touch if the touch was an edge swipe ok

int mouseortouch=0,mouseortouchprev=0;//0=mouse,mousedelay=touch

int sx,sy,ex,ey;//things of redraw area

long tstomsec(struct timespec t){//convert timespec to milliseconds
	return t.tv_sec*1000+t.tv_nsec/1000000;
}

int** init2d(){//used to initialize 2d arrays
	int **thing;
	thing=(int **)malloc(sizeof(int*)*tlength);
	for(i=0;i<tlength;i++)
		thing[i]=(int *)calloc(tt,sizeof(int));
	return thing;
}

void addtobound(int x,int y,int x2,int y2){
	if(sx==-1||sx>x)sx=x-2;
	if(sx==-1||sx>x2)sx=x2-2;
	if(sy==-1||sy>y)sy=y-2;
	if(sy==-1||sy>y2)sy=y2-2;
	if(ex==-1||ex<x)ex=x+2;
	if(ex==-1||ex<x2)ex=x2+2;
	if(ey==-1||ey<y)ey=y+2;
	if(ey==-1||ey<y2)ey=y2+2;
	sx=sx>0?(sx<sw?sx:sw-1):0;
	sy=sy>0?(sy<sh?sy:sh-1):0;
	ex=ex>0?(ex<sw?ex:sw-1):0;
	ey=ey>0?(ey<sh?ey:sh-1):0;
}

void backgroundshell(char string[]){//run a command in a fork
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

int touchnumfromdetail(int detail){//get the finger of a touch from the detail provided by the xinput2 event
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

void touchmax(int dev,int* xmax,int* ymax){//get the maximum value of the touch axes
	int i;
	for(i=0;i<4;i++)//if it's stored already
		if(tmax[i][0]==dev){
			*xmax=tmax[i][1];
			*ymax=tmax[i][2];
			return;
		}
	int ndev;
	XIDeviceInfo* xdev=XIQueryDevice(disp,dev,&ndev);//get the device info
	for(i=0;i<xdev->num_classes;i++){//iterate through classes
		if(xdev->classes[i]->type!=XIValuatorClass)//touch axes are valuators
			continue;
		XIValuatorClassInfo *val=(XIValuatorClassInfo*)xdev->classes[i];//cast
		if(!val->label)
			continue;
		char* label=XGetAtomName(disp,val->label);//get the label of the class
		if(strcmp("Abs MT Position X",label)==0){
			*xmax=(int)val->max;
			int j;
			for(j=0;j<4;j++){//store the value so the program doesn't have to get the device info every frame
				if(tmax[j][0]==dev || tmax[j][0]==-1){
					tmax[j][0]=dev;
					tmax[j][1]=*xmax;
					break;
				}
			}
		}
		if(strcmp("Abs MT Position Y",label)==0){
			*ymax=(int)val->max;
			int j;
			for(j=0;j<4;j++){
				if(tmax[j][0]==dev || tmax[j][0]==-1){
					tmax[j][0]=dev;
					tmax[j][2]=*ymax;
					break;
				}
			}
		}
	}
}

int stringtoint(char string[]){//convert a string to an integer
	int result=0,neg=0;
	unsigned int i;
	for(i=0;i<strlen(string);i++){
		if(string[i]==10)continue;
		if(i==0 && string[i]=='-'){
			neg=1;
			continue;
		}
		if(string[i]<'0' || string[i]>'9'){
			printf("Invalid character '%s' (%d) in '%s', expected number\n",&string[i],string[i],string);
			exit(1);
		}
		result=result*10+string[i]-'0';
	}
	if(neg)
		result*=-1;
	return result;
}

void stringtoedgeswipes(char string[],int index){//just put the string into the edgeswipes array
	edgeswipes[index]=malloc(strlen(string)+1);
	strcpy(edgeswipes[index],string);
}

void parseconfig(FILE * conf){
	char configbuff[255],*name,*val,*buff;
	while(fgets(configbuff,255,conf)!=NULL){
		buff=configbuff;
		if(!(name=strsep(&buff," ")))
			continue;
		if(!(val=strsep(&buff,"\n")))
			continue;
		if(strcmp("device",name)==0)
			device=stringtoint(val);
		if(strcmp("hidemouse",name)==0)
			hidemouse=stringtoint(val);
		if(strcmp("mousedelay",name)==0)
			mousedelay=stringtoint(val);
		if(strcmp("mousedevice",name)==0)
			mousedevice=stringtoint(val);
		if(strcmp("buttonlisten",name)==0)
			buttonlisten=stringtoint(val);
		if(strcmp("rightclickonhold",name)==0)
			rightclickonhold=stringtoint(val);
		if(strcmp("rightclicktime",name)==0)
			rightclicktime=stringtoint(val);
		if(strcmp("shapechangeifrightclick",name)==0)
			shapechangeifrightclick=stringtoint(val);
		if(strcmp("rightclickonend",name)==0)
			rightclickonend=stringtoint(val);
		if(strcmp("rightclickontwofingertap",name)==0)
			rightclickontwofingertap=stringtoint(val);
		if(strcmp("rightclickmethod",name)==0){
			if(strcmp("xtest",val)==0)
				rightclickmethod=0;
			else if(strcmp("command",val)==0)
				rightclickmethod=1;
		}
		if(strcmp("rightclickcommand",name)==0){
			rightclickcommand=malloc(strlen(val)+1);
			strcpy(rightclickcommand,val);
		}
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
		if(strcmp("releasedecay",name)==0)
			releasedecay=stringtoint(val);
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
		if(strcmp("outputwindow",name)==0){
			if(strcmp("none",val)==0)
				outputwindow=0;
			else if(strcmp("root",val)==0)
				outputwindow=1;
			else if(strcmp("compositeoverlay",val)==0)
				outputwindow=2;
		}
		if(strcmp("clearmethod",name)==0){
			if(strcmp("none",val)==0)
				clearmethod=0;
			else if(strcmp("expose",val)==0)
				clearmethod=1;
			else if(strcmp("cleararea",val)==0)
				clearmethod=2;
			else if(strcmp("exposeandcleararea",val)==0)
				clearmethod=3;
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

void draw(){//handles all of the actual drawing each frame
	sx=-1;sy=-1;ex=-1;ey=-1;//init the bounding rect
	for(i=0;i<tt;i++){
		if(d[0][i]){
			if(shapeaftertap || r[tlength-1][i]){//should it draw the current touch
				addtobound(tx[0][i]-r[0][i]/2,ty[0][i]-r[0][i]/2,tx[0][i]+r[0][i]/2,ty[0][i]+r[0][i]/2);
				//determin which shape the thing should be
				ev=shape;
				if(rightclickonhold && rightclickonend && shapechangeifrightclick && d[tlength-1][0] && r[tlength-1][0] && tstomsec(tsbuff)-tstomsec(timestamps[0])>(long)rightclicktime){
					if(ev==0)
						ev=1;
					else if(ev==1)
						ev=0;
				}
				if(ev==0)
					XDrawArc(disp,window,gc,tx[0][i]-r[0][i]/2,ty[0][i]-r[0][i]/2,r[0][i],r[0][i],0,360*64);
				if(ev==1)
					XDrawRectangle(disp,window,gc,tx[0][i]-r[0][i]/2,ty[0][i]-r[0][i]/2,r[0][i],r[0][i]);
			}
			if(trail && (trailduringtap || !r[tlength-1][i])){//should it draw the trail
				int flag=0;
				for(j=traildispersion;j<tlength-1;j+=traildispersion){
					if(!d[j][i])
						break;
					if(!flag && trailstartdistance>0)
						if(sqrt((double)((tx[j][i]-tx[0][i])*(tx[j][i]-tx[0][i])+(ty[j][i]-ty[0][i])*(ty[j][i]-ty[0][i])))<(double)trailstartdistance)//is it outside the start distance yet
							continue;
					if(!trailisshape){//draw a line
						int startx=tx[j-traildispersion][i],starty=ty[j-traildispersion][i];
						if(!flag && trailstartdistance>0){
							//ok this is the line where j-traildispersion is inside the trailstartdistance and j is outside it
							//so all this code is to truncate the line to only the part outside the circle
							int dist1=sqrt((double)((tx[j][i]-tx[0][i])*(tx[j][i]-tx[0][i])+(ty[j][i]-ty[0][i])*(ty[j][i]-ty[0][i])))-(double)trailstartdistance;
							int dist2=(double)trailstartdistance-sqrt((double)((tx[j-traildispersion][i]-tx[0][i])*(tx[j-traildispersion][i]-tx[0][i])+(ty[j-traildispersion][i]-ty[0][i])*(ty[j-traildispersion][i]-ty[0][i])));
							int lenx=(tx[j][i]-tx[j-traildispersion][i]);
							int leny=(ty[j][i]-ty[j-traildispersion][i]);
							int len=sqrt((double)(lenx*lenx+leny*leny));
							//dist1 is the distance along the line that's definitely outside the circle and dist2 is the distance from the other side that's definitely in the circle, i want the point that's on the circle so this narrows the options a bit
							//lenx, leny, and len are used to store the line length
							if(len>1){//don't even bother if it's not at least 1
								if(abs(leny)>abs(lenx)){//use y as linear interpolation thing
									int start=leny*dist2/len;
									int end=leny*(len-dist1)/len;
									if(start>end){
										int stor=end;
										end=start;
										start=stor;
									}
									int it,xx=tx[j-traildispersion][i]+lenx*start/leny,yy=ty[j-traildispersion][i]+start;
									//ok now just loop through every pixel and check the distance from the circle's center
									for(it=start;it<end;it++){
										xx=tx[j-traildispersion][i]+lenx*it/leny;//yay for linear interpolation
										yy=ty[j-traildispersion][i]+it;
										if(fabs(sqrt((double)((xx-tx[0][i])*(xx-tx[0][i])+(yy-ty[0][i])*(yy-ty[0][i])))-(double)trailstartdistance)<=(double)1)
											break;//k this is a good place to stop
									}
									startx=xx;
									starty=yy;
								}else{//use x it's better
									int start=lenx*dist2/len;
									int end=lenx*(len-dist1)/len;
									if(start>end){
										int stor=end;
										end=start;
										start=stor;
									}
									int it,xx=tx[j-traildispersion][i]+start,yy=ty[j-traildispersion][i]+leny*start/lenx;
									for(it=start;it<end;it++){
										xx=tx[j-traildispersion][i]+it;
										yy=ty[j-traildispersion][i]+leny*it/lenx;
										if(fabs(sqrt((double)((xx-tx[0][i])*(xx-tx[0][i])+(yy-ty[0][i])*(yy-ty[0][i])))-(double)trailstartdistance)<=(double)1)
											break;
									}
									startx=xx;
									starty=yy;
								}
							}
						}
						addtobound(startx,starty,tx[j][i],ty[j][i]);
						XDrawLine(disp,window,gc,startx,starty,tx[j][i],ty[j][i]);
					}else{//draw a shape
						addtobound(tx[j][i]-r[j][i]/2,ty[j][i]-r[j][i]/2,tx[j][i]+r[j][i]/2,ty[j][i]+r[j][i]/2);
						if(trailshape==0)
							XDrawArc(disp,window,gc,tx[j][i]-r[j][i]/2,ty[j][i]-r[j][i]/2,r[j][i],r[j][i],0,360*64);
						if(trailshape==1)
							XDrawRectangle(disp,window,gc,tx[j][i]-r[j][i]/2,ty[j][i]-r[j][i]/2,r[j][i],r[j][i]);
					}
					flag=1;
				}
			}
		}else{
			if(releasedecay>0){
				if(shapeaftertap || r[tlength-1][i]){
					double progress=tstomsec(tsbuff)-tstomsec(timestamps[tt+i]);
					if(progress<(double)releasedecay){
						int radius=(int)(((double)r[0][i])*(1-progress/((double)releasedecay)));
						addtobound(tx[0][i]-radius/2,ty[0][i]-radius/2,tx[0][i]+radius/2,ty[0][i]+radius/2);
						ev=shape;
						if(rightclickonhold && rightclickonend && shapechangeifrightclick && d[tlength-1][0] && r[tlength-1][0] && tstomsec(timestamps[tt])-tstomsec(timestamps[0])>(long)rightclicktime){
							if(ev==0)
								ev=1;
							else if(ev==1)
								ev=0;
						}
						if(ev==0)
							XDrawArc(disp,window,gc,tx[0][i]-radius/2,ty[0][i]-radius/2,radius,radius,0,360*64);
						if(ev==1)
							XDrawRectangle(disp,window,gc,tx[0][i]-radius/2,ty[0][i]-radius/2,radius,radius);
					}
				}
			}
		}
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
	tlength=traillength*traildispersion+2;
	if(tlength<4)tlength=4;//have at least two prevtouches stored
	//traillength*traildispersion+2 because the first slot stores the current touch, there are traillength*traildispersion more that are part of the trail, and the last one holds the initial conditions of the touch
	
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
	if(outputwindow==1 || outputwindow==0)//if 0 set it to root just to have it defined and for other things
		window=root;
	else if(outputwindow==2){
		if(!XQueryExtension(disp,"Composite",&i,&j,&ev)){//i, j, ev because they're unused ints at the moment
			fprintf(stderr,"XComposite extension not available\n");
			exit(1);
		}
		window=XCompositeGetOverlayWindow(disp,root);
	}
	
	if(rightclickonhold){
		if(!XQueryExtension(disp,"XTEST",&i,&j,&ev)){//i, j, ev because they're unused ints at the moment
			fprintf(stderr,"XTEST extension not available\n");
			exit(1);
		}
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
		if(hidemouse){
			sprintf(filepath,"/dev/input/event%d",mousedevice);
			f=open(filepath,O_RDONLY|O_NONBLOCK);
			if(f<0){
				fprintf(stderr,"Could not open %s, probably a permissions issue\n",filepath);
				return 1;
			}
			//init evdev
			ev=libevdev_new_from_fd(f,&devmouse);
			if(ev<0){
				fprintf(stderr,"Libevdev failed to initialize on the input stream\n");
				return 1;
			}
			
		}
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
		XIEventMask xiem[3];
		XIEventMask *mask;
		mask=&xiem[0];
		mask->deviceid=((device==-1)?XIAllMasterDevices:device);
		mask->mask_len=XIMaskLen(XI_LASTEVENT);
		mask->mask=calloc(mask->mask_len,sizeof(char));
		XISetMask(mask->mask,XI_RawTouchBegin);
		XISetMask(mask->mask,XI_RawTouchUpdate);
		XISetMask(mask->mask,XI_RawTouchEnd);
		mask=&xiem[1];
		mask->deviceid=((device==-1)?XIAllDevices:device);
		mask->mask_len=XIMaskLen(XI_LASTEVENT);
		mask->mask=calloc(mask->mask_len,sizeof(char));
		XISetMask(mask->mask,XI_TouchBegin);
		XISetMask(mask->mask,XI_TouchUpdate);
		XISetMask(mask->mask,XI_TouchEnd);
		XISetMask(mask->mask,XI_TouchOwnership);
		if(hidemouse==1){
			if(mousedevice!=device){
				mask=&xiem[2];
				mask->deviceid=((mousedevice==-1)?XIAllMasterDevices:mousedevice);
				mask->mask_len=XIMaskLen(XI_LASTEVENT);
				mask->mask=calloc(mask->mask_len,sizeof(char));
			}
			XISetMask(mask->mask,XI_RawMotion);
			if(buttonlisten)
				XISetMask(mask->mask,XI_RawButtonPress);
		}
		XISelectEvents(disp,root,&xiem[0],((hidemouse==1 && mousedevice!=device)?3:2));
		XSync(disp,False);
		tt=10;//just gonna uh do that
		//i don't feel like learning how to scan devices and pick out the number of touches from stuff
		//you're not going to have more than 10 touches typically anyways ok
		//and even if i did learn, there's an option for unlimited touches so it would get really complex fast
		
		//now set up the array for storing touch maxes
		tmax=(int **)malloc(sizeof(int*)*4);//i think 4's a good length, i don't think there will be any situation with more than 4 touchscreens at once
		for(i=0;i<4;i++){
			tmax[i]=(int *)calloc(3,sizeof(int));//device id, xmax, ymax
			tmax[i][0]=-1;//init
		}
	}
	
	//init touch arrays
	tx=init2d();//x
	ty=init2d();//y
	d=init2d();//is touched
	r=init2d();//width
	timestamps=calloc(tt*2,sizeof(struct timespec));//first tt indices are for touch start, last tt are for touch end
	//on the last index, r will be used as a switch for tapthreshold and d will be used as a switch for detecting if only one button has been held for the entire touch
	
	if(clearmethod==1 || clearmethod==3){
		//set up expose event
		memset(&xev,0,sizeof(xev));
		xev.type=Expose;
		xev.serial=0;
		xev.send_event=True;
		xev.display=disp;
		xev.window=window;
		xev.count=0;
	}
	
	//init graphics context
	if(outputwindow!=0){
		gcval.foreground=XWhitePixel(disp,0);
		gcval.background=XBlackPixel(disp,0);
		gcval.function=GXxor;
		gcval.plane_mask=gcval.background^gcval.foreground;
		gcval.subwindow_mode=IncludeInferiors;
		gc=XCreateGC(disp,window,GCFunction|GCForeground|GCBackground|GCSubwindowMode,&gcval);
	}
	
	while(1){
		//get the time ok
		if(rightclickonhold || releasedecay>0)
			if(clock_gettime(CLOCK_MONOTONIC,&tsbuff)!=0)
				fprintf(stderr,"There was an error getting the current timestamp\n");
		
		//get input
		if(inputmethod==0){
			if(hidemouse)
				do{//make sure libevdev is completely updated on the mouse events
					ev=libevdev_next_event(devmouse,LIBEVDEV_READ_FLAG_NORMAL,&e);
					if(ev==0)
						mouseortouch=mouseortouchprev-1;
				}while(ev==0);//these events aren't actually used, rather the events just trigger the mouse being shown if there are no touch events on the same frame
			do{//make sure libevdev is completely updated on the device events
				ev=libevdev_next_event(dev,LIBEVDEV_READ_FLAG_NORMAL,&e);
				if(ev==0)
					mouseortouch=mousedelay;
			}while(ev==0);
		}else if(inputmethod==1){
			while(XPending(disp)){
				XNextEvent(disp,&xevent);
				if(xevent.xcookie.type==GenericEvent && xevent.xcookie.extension==xiopcode && XGetEventData(disp,&xevent.xcookie)){
					xiev=xevent.xcookie.data;
					switch(xevent.xcookie.evtype){
						case XI_RawButtonPress:
						case XI_RawMotion:
							if(xevent.xcookie.evtype==XI_RawMotion || (xevent.xcookie.evtype==XI_RawButtonPress && (xiev->detail==1 || xiev->detail==2 || xiev->detail==3)))
								if(mouseortouch==mouseortouchprev)
									mouseortouch=mouseortouchprev-1;
							break;
						case XI_TouchBegin:
						case XI_TouchUpdate:
							if((tindex=touchnumfromdetail(xiev->detail))<0)
								break;
							if(tindex>=tt)
								break;
							d[0][tindex]=1;
							r[0][tindex]=width;
							tx[0][tindex]=xiev->event_x;
							ty[0][tindex]=xiev->event_y;
							mouseortouch=mousedelay;
							break;
						case XI_RawTouchBegin:
						case XI_RawTouchUpdate:
							if((tindex=touchnumfromdetail(xiev->detail))<0)
								break;
							if(tindex>=tt)
								break;
							int xmax,ymax;
							touchmax(xiev->sourceid,&xmax,&ymax);
							d[0][tindex]=1;
							r[0][tindex]=width;
							tx[0][tindex]=xiev->event_x*sw/xmax;
							ty[0][tindex]=xiev->event_y*sh/ymax;
							mouseortouch=mousedelay;
							break;
						case XI_TouchEnd:
						case XI_RawTouchEnd:
							if((tindex=touchnumfromdetail(xiev->detail))<0)
								break;
							if(tindex>=tt)
								break;
							d[0][tindex]=0;
							break;
					}
				}
				XFreeEventData(disp,&xevent.xcookie);
			}
		}
		for(i=0;i<tt;i++){
			//get the values if libevdev
			if(inputmethod==0){
				d[0][i]=(libevdev_get_slot_value(dev,i,57)!=-1);//is touch actually
				if(d[0][i]){
					tx[0][i]=libevdev_get_slot_value(dev,i,53)*sw/tw;//get touch coordinatess
					ty[0][i]=libevdev_get_slot_value(dev,i,54)*sh/th;
					if(fixedwidth==0){//if fixed width is off, adjust width
						r[0][i]=libevdev_get_slot_value(dev,i,48)*widthmult;//what width is
					}else{
						r[0][i]=width;
					}
				}
			}
			if(d[0][i] && !d[1][i]){
				//thing to store info about touch start
				tx[tlength-1][i]=tx[0][i];
				ty[tlength-1][i]=ty[0][i];
				r[tlength-1][i]=1;//tap threshold flag
				d[tlength-1][i]=d[0][i];
				if(rightclickonhold)
					timestamps[i]=tsbuff;
			}
			if(!d[0][i] && d[1][i]){
				if(releasedecay>0)
					timestamps[i+tt]=tsbuff;
			}
			if(d[0][i])//tap threshold
				if(sqrt((double)((tx[0][i]-tx[tlength-1][i])*(tx[0][i]-tx[tlength-1][i])+(ty[0][i]-ty[tlength-1][i])*(ty[0][i]-ty[tlength-1][i])))>=(double)tapthreshold)//check tap threshold
					r[tlength-1][i]=0;
		}
		
		//draw the things
		if(outputwindow!=0)
			draw(0);
		
		//edge swipes
		//if edgeswipeextrapolate, start on the second frame of the touch, else start on the first frame of touch
		if(d[0][0] && (edgeswipeextrapolate?(d[1][0] && !d[2][0]):(!d[1][0])) && d[tlength-1][0]){
			int exx=tx[1][0]-(edgeswipeextrapolate?tx[0][0]-tx[1][0]:0),//if edgeswipeextrapolate try to guess the touch at the frame before
				exy=ty[1][0]-(edgeswipeextrapolate?ty[0][0]-ty[1][0]:0);//extrapolating the touch helps for faster swipes
			es=0;
			if(!edgeswipeextrapolate || abs(ty[0][0]-ty[1][0])>=abs(tx[0][0]-tx[1][0])){//only check the edge if the swipe is mostly perpendicular to the edge
				if(exy<=edgeswipethreshold){
					es|=1;//set the flag so the program doesn't have to go through this again when the touch ends
					backgroundshell(edgeswipes[0]);//then execute the configured command
				}
				if(exy>=sh-1-edgeswipethreshold){
					es|=2;
					backgroundshell(edgeswipes[1]);
				}
			}
			if(!edgeswipeextrapolate || abs(ty[0][0]-ty[1][0])<=abs(tx[0][0]-tx[1][0])){
				if(exx<=edgeswipethreshold){
					es|=4;
					backgroundshell(edgeswipes[2]);
				}
				if(exx>=sw-1-edgeswipethreshold){
					es|=8;
					backgroundshell(edgeswipes[3]);
				}
			}
		}
		//end edge swipe gestures
		//only trigger when the touch has ended and only the first finger has been down for the whole touch
		if(!d[0][0] && d[1][0] && d[tlength-1][0]){
			if(es&1)
				backgroundshell(edgeswipes[4]);
			if(es&2)
				backgroundshell(edgeswipes[5]);
			if(es&4)
				backgroundshell(edgeswipes[6]);
			if(es&8)
				backgroundshell(edgeswipes[7]);
		}
		
		if(mouseortouch<0)
			mouseortouch=0;
		//hide/show mouse
		if(hidemouse){
			if(mouseortouch==mousedelay && mouseortouchprev==0)
				XFixesHideCursor(disp,window);
			if(mouseortouch==0 && mouseortouchprev!=0)
				XFixesShowCursor(disp,window);
		}
		
		if(d[0][0] && d[tlength-1][0]){
			for(i=1;i<tt && !d[0][i];i++){}//check if all the other touches are not held down
			if(i<tt)
				d[tlength-1][0]=0;
		}
		
		//rightclick on tap hold
		if(rightclickonhold){
			if((rightclickonend?!d[0][0]:d[0][0]) && d[1][0]){
				//d[tlength-1][0] is the flag that means only the first finger has been held down the entire time
				//r[tlength-1][0] is the flag that means the finger hasn't moved outside the tap threshold
				//timestamps[0] is the stored time of the beginning of the touch
				//then just compare the difference in times to the rightclicktime setting
				if(d[tlength-1][0] && r[tlength-1][0] && tstomsec(tsbuff)-tstomsec(timestamps[0])>(long)rightclicktime){
					if(rightclickmethod==0){
						XTestFakeButtonEvent(disp,3,True,0);//rightclick down
						XTestFakeButtonEvent(disp,3,False,0);//rightclick up
					}else{
						backgroundshell(rightclickcommand);
					}
					d[tlength-1][0]=0;
				}
			}
		}
		//rightclick on two finger tap
		if(rightclickontwofingertap){
			if(!d[0][0] && !d[0][1] && !d[0][2] && r[tlength-1][0] && r[tlength-1][1] && tstomsec(tsbuff)-tstomsec(timestamps[0])<(long)100 && tstomsec(tsbuff)-tstomsec(timestamps[1])<(long)100 && tstomsec(tsbuff)-tstomsec(timestamps[2])>500){
				if(rightclickmethod==0){
					XTestFakeButtonEvent(disp,3,True,0);//rightclick down
					XTestFakeButtonEvent(disp,3,False,0);//rightclick up
				}else{
					backgroundshell(rightclickcommand);
				}
				r[tlength-1][1]=0;//prevent it from happening more than once
			}
		}
		
		mouseortouchprev=mouseortouch;
		
		XFlush(disp);
		
		//advance the buffer
		for(i=0;i<tt;i++)
			for(j=tlength-3;j>=0;j--){
				tx[j+1][i]=tx[j][i];
				ty[j+1][i]=ty[j][i];
				r[j+1][i]=r[j][i];
				d[j+1][i]=d[j][i];
			}
		
		waitpid((pid_t)-1,&ev,WNOHANG);//stupid zombies
		if(fps!=0)
			usleep(1000000/fps);
		
		//clear the drawn areas
		if(outputwindow!=0){
			if(ex-sx!=0 || ey-sy!=0){
				if(clearmethod==1 || clearmethod==3){
					xev.x=sx;
					xev.y=sy;
					xev.width=ex-sx+1;
					xev.height=ey-sy+1;
					XSendEvent(disp,window,True,ExposureMask,(XEvent*)&xev);
					XFlush(disp);
				}
				if(clearmethod==2 || clearmethod==3){
					XClearWindow(disp,window);
					XClearArea(disp,window,sx,sy,ex-sx,ey-sy,True);
					XFlush(disp);
				}
			}
		}
	}
	
	XCloseDisplay(disp);
	return 0;
}
