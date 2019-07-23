/*
 *  Printer Application Framework.
 * 
 *  This is the detection and PPD matcher of the Printer Application.
 *
 *  Copyright 2019 by Dheeraj.
 *
 *  Licensed under Apache License v2.0.  See the file "LICENSE" for more
 *  information.
 */
#include "server.h"
#include <sys/socket.h>

char *snap;
char *tmpdir; //SNAP_COMMON

static void DEBUG(char* x)
{
    static int counter =0;
    fprintf(stderr,"DEBUG[%d]: %s\n",counter++,x);
}

static void escape_string(char* out,char* in,int len)
{
  int i;
  for(i=0;i<(len-1)&&in[i];i++)
  {
    if(!isalnum(in[i]))
      out[i]='-';
    else
      out[i]=in[i];
  }
  out[i]=0;
}

/*
 * TODO: Can we use siginfo->st_mtime to prevent overcrowding?
 */
static void manage_device(int sig,siginfo_t *siginfo,void* context)
{
  union sigval sigtype = siginfo->si_value;
  int signal_data = sigtype.sival_int;
  fprintf(stderr, "Recieved Signal: %d\n",signal_data);
  if(signal_data&&signal_data%2==0)
    get_devices(0); //Remove devices
  else
    get_devices(1);   //Insert devices
}

void cleanup()
{
  cupsArrayDelete(con_devices);
  cupsArrayDelete(temp_devices);
}
static void kill_main(int sig,siginfo_t *siginfo,void* context){
  printf("DEBUG: KILLING Printer Application!\n");
  cleanup();
  exit(0);
}
static int signal_listeners()
{
  struct sigaction device_act;
  memset(&device_act,'\0',sizeof(device_act));
  device_act.sa_sigaction = &manage_device;
  device_act.sa_flags = SA_SIGINFO;
  if(sigaction(SIGUSR1,&device_act,NULL)<0){
    fprintf(stderr,"ERROR: Unable to start usb detector!\n");
    return 1;
  }
  struct sigaction kill_act;
  memset(&kill_act,'\0',sizeof(kill_act));
  kill_act.sa_sigaction = &kill_main;
  kill_act.sa_flags = SA_SIGINFO;
  if(sigaction(SIGHUP,&kill_act,NULL)<0){
    fprintf(stderr,"ERROR: Unable to set cleanup process!\n");
    return 1;
  }
  if(sigaction(SIGINT,&kill_act,NULL)<0){
    fprintf(stderr,"ERROR: Unable to set cleanup process!\n");
    return 1;
  }
  return 0;
}

void start_hardware_monitor(pid_t ppid)
{
  int status = 0;
  if((status=prctl(PR_SET_PDEATHSIG,SIGTERM))<0){
    perror(0);    /*Unable to set prctl*/
    exit(1);
  }
  if(getppid() != ppid)
    exit(0);      /*Parent as exited already!*/
  monitor_devices(ppid);  /*Listen for usb devices!*/
}

#ifdef HAVE_AVAHI
void start_avahi_monitor(pid_t ppid)
{
  int status = 0;
  if((status=prctl(PR_SET_PDEATHSIG,SIGTERM))<0){
    perror(0);    /*Unable to set prctl*/
    exit(1);
  }
  if(getppid() != ppid)
    exit(0);      /*Parent as exited already!*/
  monitor_avahi_devices(ppid);  /*Listen for usb devices!*/
}
#endif

/*
 * main() -
 */

int main(int argc,char* argv[])
{
  // fprintf(stderr,"Port: %d\n",getport());
  pid_t pid,ppid;
  ppid = getpid();

  if(getenv("SNAP"))
  {
    snap = strdup(getenv("SNAP"));
  }
  else snap =strdup("");

  if(getenv("SNAP_COMMON"))
    tmpdir = strdup(getenv("SNAP_COMMON"));
  else tmpdir = strdup("/var/tmp");
  
  con_devices = cupsArrayNew((cups_array_func_t)compare_devices,NULL);
  temp_devices = cupsArrayNew((cups_array_func_t)compare_devices,NULL);

  if((pid=fork())==0){
    start_hardware_monitor(ppid);
  }
  else if(pid<0)
  {
    perror(0);    /* Unable to fork! */
    exit(1);
  }
  #if HAVE_AVAHI
  if((pid=fork())==0){
    start_avahi_monitor(ppid);
  }
  else if(pid<0){
    perror(0);
    exit(1);
  }
  #endif
  if(signal_listeners())  /*Set signal listeners in parent*/ 
    return 1;

  while(1)            /*Infinite loop*/
    sleep(1000000);

  cleanup();
  
  return 0;
}

static int 
get_devices(int insert)

{
    const char  *serverbin; // ServerBin
    char        program[2048];     // Full Path to program
    char        *argv[7];
    char        *env[7];
    char        name[32],reques_id[16],limit[16],
                timeout[16],user_id[16],options[1024];
    char        serverdir[1024];
    process_t   *process;
    int         process_pid,status;
    
    cupsArrayClear(temp_devices);
    // fprintf(stdout,"SIZE: temp: %d\n",cupsArrayCount(temp_devices));
    if((process = calloc(1,sizeof(process_t)))==NULL)
    {
      fprintf(stderr,"ERROR: Ran Out of Memory!\n");
      return (-1);
    }

    strcpy(name,"deviced");
    strcpy(reques_id,DEVICED_REQ);
    strcpy(limit,DEVICED_LIM);
    strcpy(timeout,DEVICED_TIM);
    strcpy(user_id,DEVICED_USE);
    strcpy(options,DEVICED_OPT);
    
    snprintf(program,sizeof(program),"%s/%s/%s",snap,BINDIR,name);
    // fprintf(stderr,"%s\n",program);
    // char cwd[1000];
    // if (getcwd(cwd, sizeof(cwd)) != NULL) {
    //     fprintf(stderr,"Current working dir: %s\n", cwd);
    // } else {
    //     perror("getcwd() error");
    //     return 1;
    // }
    snprintf(serverdir,sizeof(serverdir),"%s/%s",snap,SERVERBIN);
    // if(_cupsFileCheck(program,_CUPS_FILE_CHECK_PROGRAM,!geteuid(),
    //                     _cupsFileCheckFilter,NULL))
    //     return (-1);

    argv[0] = (char*) name;
    // argv[1] = (char*) reques_id;
    // argv[2] = (char*) limit;
    // argv[3] = (char*) timeout;
    // argv[4] = (char*) user_id;
    // argv[5] = (char*) options;
    // argv[6] = NULL;
    argv[1] = (char*) limit;
    argv[2] = (char*) timeout;
    argv[3] = (char*)"";
    argv[4] = NULL;

    env[0]  = (char*) serverdir;
    env[1]  = NULL;
    setenv("CUPS_SERVERBIN",serverdir,1);
    // DEBUG(program);
    // fprintf(stdout,"Running deviced!\n");
    if((process->pipe = cupsdPipeCommand2(&(process->pid),program,argv,
                            0))==NULL)
    {
        fprintf(stderr,"ERROR: Unable to execute deviced!\n");
        return (-1);
    }
    if((process_pid = waitpid(process->pid,&status,0))>0)
    {
        if(WIFEXITED(status))
        {
            // do{
            //     parse_line(process);
            // }
            // while(_cupsFilePeekAhead(process->pipe,'\n'));
            while(!parse_line(process));
        }
    }
    // fprintf(stderr,"DEBUG: Found Devices!!!\n");
    //waitpid(-1,NULL,WNOHANG);   // Clean up 
    if(insert)
    {
      // fprintf(stderr,"DEBUG: Adding Devices!\n");
      add_devices(con_devices,temp_devices);
    }
    else{
      remove_devices(con_devices,temp_devices);
    }
    free(process);
    return (0);
}

// static int parse_line(process_t *backend){
//   char line[2048];
//   if(cupsFileGetLine(backend->pipe,line,sizeof(line))){
//     fprintf(stdout,"Line :%s\n\n",line);
//     return 0;
//   }
//   return 1;
// }
static int				/* O - 0 on success, -1 on error */
parse_line(process_t *backend)	/* I - Backend to read from */
{
  char	line[2048],			/* Line from backend */
	temp[2048],			/* Copy of line */
	*ptr,				/* Pointer into line */
	*dclass,			/* Device class */
	*uri,				/* Device URI */
	*make_model,			/* Make and model */
	*info,				/* Device info */
	*device_id,			/* 1284 device ID */
	*location;			/* Physical location */

if (cupsFileGets(backend->pipe, line, sizeof(line)))
  {
   /*
    * Each line is of the form:
    *
    *   class URI "make model" "name" ["1284 device ID"] ["location"]
    */

    strlcpy(temp, line, sizeof(temp));
    // fprintf(stdout,"%s\n",line);
   /*
    * device-class
    */

    dclass = temp;

    for (ptr = temp; *ptr; ptr ++)
      if (isspace(*ptr & 255))
        break;

    while (isspace(*ptr & 255))
      *ptr++ = '\0';

   /*
    * device-uri
    */

    if (!*ptr)
      goto error;

    for (uri = ptr; *ptr; ptr ++)
      if (isspace(*ptr & 255))
        break;

    while (isspace(*ptr & 255))
      *ptr++ = '\0';

   /*
    * device-make-and-model
    */

    if (*ptr != '\"')
      goto error;

    for (ptr ++, make_model = ptr; *ptr && *ptr != '\"'; ptr ++)
    {
      if (*ptr == '\\' && ptr[1])
        _cups_strcpy(ptr, ptr + 1);
    }

    if (*ptr != '\"')
      goto error;

    for (*ptr++ = '\0'; isspace(*ptr & 255); *ptr++ = '\0');

   /*
    * device-info
    */

    if (*ptr != '\"')
      goto error;

    for (ptr ++, info = ptr; *ptr && *ptr != '\"'; ptr ++)
    {
      if (*ptr == '\\' && ptr[1])
        _cups_strcpy(ptr, ptr + 1);
    }

    if (*ptr != '\"')
      goto error;

    for (*ptr++ = '\0'; isspace(*ptr & 255); *ptr++ = '\0');

   /*
    * device-id
    */

    if (*ptr == '\"')
    {
      for (ptr ++, device_id = ptr; *ptr && *ptr != '\"'; ptr ++)
      {
	if (*ptr == '\\' && ptr[1])
	  _cups_strcpy(ptr, ptr + 1);
      }

      if (*ptr != '\"')
	goto error;

      for (*ptr++ = '\0'; isspace(*ptr & 255); *ptr++ = '\0');

     /*
      * device-location
      */

      if (*ptr == '\"')
      {
	for (ptr ++, location = ptr; *ptr && *ptr != '\"'; ptr ++)
	{
	  if (*ptr == '\\' && ptr[1])
	    _cups_strcpy(ptr, ptr + 1);
	}

	if (*ptr != '\"')
	  goto error;

	*ptr = '\0';
      }
      else
        location = NULL;
    }
    else
    {
      device_id = NULL;
      location  = NULL;
    }

   /*
    * Add the device to the array of available devices...
    */
    process_device(dclass, make_model, info, uri, device_id, location);
      // fprintf(stderr, "DEBUG: Found device \"%s\"...\n", uri);

    return (0);
  }

 /*
  * End of file...
  */

  cupsFileClose(backend->pipe);
  backend->pipe = NULL;

  return (-1);

 /*
  * Bad format; strip trailing newline and write an error message.
  */

  error:

  if (line[strlen(line) - 1] == '\n')
    line[strlen(line) - 1] = '\0';

  fprintf(stderr, "ERROR: [deviced] Bad line from \"%s\": %s\n",
	  backend->name, line);
  return (0);
}

/*
 *  'compare_devices()' - Compare device uri.
 */
static int
compare_devices(device_t *d0,
                device_t *d1)
{
  int diff=  strcasecmp(d0->device_uri,d1->device_uri);
//  fprintf(stdout,"%s %s %d\n",d0->device_uri,d1->device_uri,diff);
  return diff;
}

static int
process_device(const char *device_class,
    const char *device_make_and_model,
    const char *device_info,
    const char *device_uri,
    const char *device_id,
    const char *device_location)
{
  device_t *device;
  if((device = calloc(1,sizeof(device_t)))==NULL)
  {
    fprintf(stderr,"ERROR: Ran out of memory!\n");
    return -1;
  }
  if(device_make_and_model)
  {
    if(!strncasecmp(device_make_and_model,"Unknown",7))
    {
      return -2;
    }
  }

  if(device_uri)
    strlcpy(device->device_uri,device_uri,sizeof(device->device_uri));
  if(device_class)
    strlcpy(device->device_class,device_class,sizeof(device->device_class));
  if(device_make_and_model)
    strlcpy(device->device_make_and_model,device_make_and_model,sizeof(device->device_make_and_model));
  if(device_info)
    strlcpy(device->device_info,device_info,sizeof(device->device_info));
  if(device_id)
    strlcpy(device->device_id,device_id,sizeof(device->device_id));
  if(device_location)
    strlcpy(device->device_location,device_location,sizeof(device->device_location));

  if(cupsArrayFind(temp_devices,device))
    free(device);
  else{
    cupsArrayAdd(temp_devices,device); // Do we need device limit????
  }
  return 0;
}

void add_devices(cups_array_t *con, cups_array_t *temp)
{
  device_t *dev = cupsArrayFirst(temp);
  char ppd[1024];
  for(;dev;dev=cupsArrayNext(temp))
  {
    if(dev==NULL){
      break;
    }
    if(!cupsArrayFind(con,dev))
    {
      // fprintf(stderr,"DEBUG: Getting PPD!\n");
      int ret = get_ppd(ppd,sizeof(ppd),dev->device_make_and_model,sizeof(dev->device_make_and_model),
                          dev->device_id,sizeof(dev->device_id));
      if(ret>=0){
        strlcpy(dev->ppd,ppd,sizeof(dev->ppd));
        // fprintf(stdout,"PPD LOC: %s\n",dev->ppd);
        cupsArrayAdd(con,dev);
        fprintf(stdout,"DEBUG: Adding Printer: %s\n",dev->device_id);
  
        start_ippeveprinter(dev);
      }
    }
  }
}

void remove_devices(cups_array_t *con,cups_array_t *temp)
{
  device_t *dev = cupsArrayFirst(con);
  char ppd[128];
  for(;dev;dev=cupsArrayNext(con))
  {
    if(cupsArrayFind(temp,dev)==NULL)
    {
      remove_ppd(dev->ppd);
      fprintf(stdout,"DEBUG: Removing Printer: %s\n",dev->device_id);
      kill_ippeveprinter(dev->eve_pid);
      cupsArrayRemove(con,dev);
      device_t *tt = dev;   // Do we need this?
      free(tt);
    }
  }
}

/*
 * Use language to filter???
 */

static int
get_ppd(char* ppd, int ppd_len,            /* O- */ 
        char *make_and_model,int make_len,
        char *device_id, int dev_len) /* I- */
{
  const char *serverbin;
  char program[2048];
  char *argv[6];
  char name[16],operation[8],request_id[4],limit[5],options[1024];
  char ppd_uri[128];
  char ppd_name[1024];  //full ppd path
  char escp_model[256];
  char *envp[6];
  char datadir[1024],serverdir[1024],cachedir[1024];

  process_t *process;
  int        process_pid,status;
  if((process = calloc(1,sizeof(process_t)))==NULL)
  {
    fprintf(stderr,"ERROR: Ran Out of Memory!\n");
    return (-1);
  }
  strcpy(name,"cups-driverd");
  strcpy(operation,"list");
  strcpy(request_id,"0");
  strcpy(limit,"1");
  snprintf(options,sizeof(options),"ppd-make-and-model=\'%s\' ppd-device-id=\'%s\'",make_and_model,device_id);
  //snprintf(options,sizeof(options),"ppd-make-and-model=\'HP\'");
  
  snprintf(datadir,sizeof(datadir),"%s/%s",snap,DATADIR);
  snprintf(serverdir,sizeof(serverdir),"%s/%s",snap,SERVERBIN);
  snprintf(cachedir,sizeof(cachedir),"%s",tmpdir);

  setenv("CUPS_DATADIR",datadir,1);
  setenv("CUPS_SERVERBIN",serverdir,1);
  setenv("CUPS_CACHEDIR",cachedir,1);
  // if((serverbin = getenv("SERVERBIN"))==NULL)
  //   serverbin = CUPS_SERVERBIN;
  snprintf(program,sizeof(program),"%s/bin/%s",snap,name);

  argv[0] = (char*) name;
  argv[1] = (char*) operation;
  argv[2] = (char*) request_id;
  argv[3] = (char*) limit;
  argv[4] = (char*) options;
  argv[5] = NULL;

  // envp[0] = (char*) datadir;
  // envp[1] = (char*) serverdir;
  // envp[2] = (char*) cachedir;
  // envp[3] = NULL;
  fprintf(stderr,"DEBUG: Executing cups-driverd at %s\n",program);
  if((process->pipe = cupsdPipeCommand2(&(process->pid),program,
                        argv,0))==NULL)
  {
    fprintf(stderr,"ERROR: Unable to execute!\n");
    return (-1);
  }
  if((process_pid = waitpid(process->pid,&status,0))>0)
  {
      if(WIFEXITED(status))
      {
          // do{          
          if(get_ppd_uri(ppd_uri,process)) //All we need is a single line!
            return (-1);
          // fprintf(stdout,"PPD-URI: %s\n",ppd_uri);
          // }
          // while(_cupsFilePeekAhead(process->pipe,'\n'));
      }
  }
  
  strcpy(operation,"cat");
  argv[2] = (char*) ppd_uri;
  argv[3] = NULL;
  argv[4] = NULL;
  
  if((process->pipe = cupsdPipeCommand2(&(process->pid),program,
                        argv,0))==NULL)
  {
    fprintf(stderr,"ERROR: Unable to execute cups-driverd!\n");
    return (-1);
  }
  
  escape_string(escp_model,make_and_model,make_len);
  char ppd_folder[2048];
  snprintf(ppd_folder,sizeof(ppd_folder),"%s/ppd",tmpdir);
  if(mkdir(ppd_folder,0777)==-1)
  {
    fprintf(stderr,"ERROR: %s\n",strerror(errno));
  }
  snprintf(ppd_name,sizeof(ppd_name),"%s/ppd/%s.ppd",tmpdir,escp_model);
  cups_file_t* tempPPD;
  if((tempPPD = cupsFileOpen(ppd_name,"w"))==NULL)
  {
    fprintf(stderr,"ERROR: Cannot create temporary PPD!\n");
    return (-1);
  }

  if((process_pid = waitpid(process->pid,&status,0))>0)
  {
      if(WIFEXITED(status))
      {
        int st =0,counter=0;
        while((st=print_ppd(process,tempPPD))>0) counter++;
      }
  }
  
  cupsFileClose(tempPPD);
  free(process);
  strncpy(ppd,ppd_name,ppd_len);
  return 0;
}

int get_ppd_uri(char* ppd_uri, process_t* backend)
{
  char line[2048];
  if (cupsFileGets(backend->pipe, line, sizeof(line)))
  {
    sscanf(line,"%s",ppd_uri);
    return 0;
  }
  return 1;
}
  
int print_ppd(process_t* backend,cups_file_t *temp)
{
  char line[2048];
  if (cupsFileGets(backend->pipe,line,sizeof(line)))
  {
    cupsFilePrintf(temp,"%s\n",line);
    return 1;
  }
  return 0;
}

int remove_ppd(char* ppd)
{
  // if(_cupsFileCheck(ppd,_CUPS_FILE_CHECK_PROGRAM,!geteuid(),
  //                   _cupsFileCheckFilter,NULL))
  //   return (-1);
  // else{
    return unlink(ppd);
 // }
}

int start_ippeveprinter(device_t *dev)
{
  pid_t pid=0,ppid=0;
  ppid = getpid();
  if((pid=fork())<0)
  {
    return -1;
  }
  else if(pid==0){
    int status = 0;
    if((status=prctl(PR_SET_PDEATHSIG,SIGTERM))<0){
      perror(0);    /*Unable to set prctl*/
      exit(1);
    }
    if(getppid() != ppid)
      exit(0);      /*Parent as exited already!*/
    
    char *argv[17];
    char name[2048],device_uri[2048],ppd[1024],make_and_model[512]
      ,command[1024],pport[8],location[3096];
    char *envp[2];
    char LD_PATH[512];
    // strncpy(LD_PATH,"LD_LIBRARY_PATH=/usr/lib",sizeof(LD_PATH));
    // setenv("LD_LIBRARY_PATH","/usr/lib",1);
    setenv("PRINTER",dev->device_make_and_model,1);
    // envp[0]=(char*)LD_PATH;
    // envp[1]=NULL;
    
    snprintf(name,sizeof(name),"%s/bin/ippeveprinter",snap);
    if(dev==NULL)
      exit(1);
    if(dev->device_uri)
      snprintf(device_uri,sizeof(device_uri),"\"%s\"",dev->device_uri);
    if(dev->ppd)
      snprintf(ppd,sizeof(ppd),"%s",dev->ppd);
    snprintf(pport,sizeof(pport),"%d",getport());
    
    snprintf(command,sizeof(command),"%s/bin/ippprint",snap);
    snprintf(location,sizeof(location),"Printer Application, Original Device Info: %s",dev->device_info);
    escape_string(make_and_model,dev->device_make_and_model,sizeof(dev->device_make_and_model));
    argv[0] = (char*)name;
    argv[1] = "-D";
    argv[2] = (char*)device_uri;
    argv[3] = "-P";
    argv[4] = (char*)ppd;
    argv[5] = "-c";
    argv[6] = (char*)command;
    argv[7] = "-p";
    argv[8] = (char*) pport;
    argv[9] = "-l";
    argv[10] = (char*)location;
    argv[11] = "-K";
    argv[12] = (char*)tmpdir;
    argv[13] = "-n";
    argv[14] = strdup("localhost");
    argv[15]= (char*)make_and_model;
    argv[16] = NULL;

    //dup2(1,2);
    char printerlogs[1024];
    snprintf(printerlogs,sizeof(printerlogs),"%s/printer.logs",tmpdir);
    int logfd = open(printerlogs,O_CREAT,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    close(logfd);
    logfd = open(printerlogs,O_WRONLY|O_APPEND);
    // fprintf(stdout,"EXEC:%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n",argv[0],argv[1],argv[2],argv[3],
    //                  argv[4],argv[5],argv[6],argv[7],argv[8],argv[9],argv[10] ,argv[11],argv[12],argv[13],
    //                  argv[14],argv[15]);
    if(logfd>0)
    {
      dup2(logfd,2);
      dup2(logfd,1);
      close(logfd);
    }
    
    execvp(argv[0],argv);

    exit(0);
  }
  if(dev)
    dev->eve_pid = pid;
  return pid;
}

int getport()
{
  int port = 8000;
  for(;port<9000;port++)
  {
    int sd=0;
    sd = socket(AF_INET,SOCK_STREAM,0);
    if(sd<0)
    {
      continue;
    }
    int true = 1;
    setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&true,sizeof(int));
    struct sockaddr_in server;
    memset(&server,0,sizeof(server));
    server.sin_family=AF_INET;
    server.sin_addr.s_addr=htonl(INADDR_ANY);
    server.sin_port=htons(port);
    int t;
    if((t=bind(sd,(struct sockaddr*)&server,sizeof(struct sockaddr)))>=0)
    {
      // fprintf(stderr,"IN: %d %d\n",sd,t);
      close(sd);
      break;
    }
    close(sd);
    // fprintf(stderr,"%d %d\n",sd,t);
  }
  return port;
}

static int kill_ippeveprinter(pid_t pid)
{
  int status;
  if(pid>0){
    fprintf(stdout,"DEBUG: Killing ippeveprinter: %d\n",pid);
    kill(pid,SIGINT);
    if(waitpid(pid,&status,0)<0)
    {
      fprintf(stderr,"ERROR: WAITPID Error!\n");
      return -1;
    }
  }
  return 0;
}