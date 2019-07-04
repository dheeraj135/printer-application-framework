#include "ippprint.h"

extern char **environ;

FILE *sout,*serr;

/*
 * getUserId() - Get uid for a given username.
 */
static int getUserId(const char *username)
{
  struct passwd *p;
  if((p=getpwnam(username))==NULL) {
    return -1;
  }
  return (int)p->pw_uid;
}

static void ini()
{
  char logs[2048];
  snprintf(logs,sizeof(logs),"%s/logs.txt",TMPDIR);
  fprintf(stdout,"%s\n",logs);
  sout = fopen(logs,"a");
  if(sout==NULL)
  {
    fprintf(stderr,"UNABLE TO OPEN!\n");
  }
  fprintf(sout,"*****************************************************\n");
}

/*
 * getFilterPath() - Get path to required filter.
 * 
 * It checks for filters int SERVERBIN/filter folder and its sub directories upto
 * a depth 1. This allows us to create a symbolic link in SERVERBIN/filter to 
 * CUPS filter directories.
 * 
 * in - Filter name
 * *out - Full path of the Filter
 * 
 * Return:
 *  0 - Success
 *  !=0 - Error
 */
static int getFilterPath(char *in, char **out)
{
  *out = NULL;
  char path[2048];
  cups_dir_t* dir;
  cups_dentry_t* dent;

  snprintf(path,sizeof(path),"%s/filter/%s",SERVERBIN,in);  /* Check if we can directly access filter*/

  if((access(path,F_OK|X_OK)!=-1)&&fileCheck(path))
  {
    *out = strdup(path);
    return 0;
  }

  snprintf(path,sizeof(path),"%s/filter",SERVERBIN);
  dir = cupsDirOpen(path);
  
  while((dent=cupsDirRead(dir)))  /*Check only upto one level*/
  {
    char *filename = dent->filename;
    if(S_ISDIR(dent->fileinfo.st_mode))
    {
      snprintf(path,sizeof(path),"%s/filter/%s/%s",SERVERBIN,filename,in);
      if((access(path,F_OK|X_OK)!=-1)&&fileCheck(path)){
        *out = strdup(path);
        break;
      }
    }
  }
  cupsDirClose(dir);

  if(*out)
    return 0;
  
  return -1;
}

/*
 * getFilterPaths() - For a given list of filters, find corresponding full paths of those filters.
 * 
 * Returns:
 * 0 - Success
 * !0 - Error 
 */
static int getFilterPaths(cups_array_t *filter_chain, cups_array_t **filter_fullname)
{
  *filter_fullname=cupsArrayNew(NULL,NULL);
  char *in,*out;
  filter_t *currFilter;
  for(currFilter=cupsArrayFirst(filter_chain);currFilter;currFilter=cupsArrayNext(filter_chain))
  {
    in = currFilter->filter;
    if(getFilterPath(in,&out)==-1)
    {
      return -1;
    }
    filter_t* temp=filterCopy(currFilter);
    free(temp->filter);
    temp->filter=out;
    cupsArrayAdd(*filter_fullname,temp);
  }
  return 0;
}

/*
 * createOptionsArray() - Create Options array from env variables.
 * 
 * Returns:
 * 0 - Success
 * !0 - Error
 */
static int createOptionsArray(char *op)  /*O-*/
{
  sprintf(op,"h");
  return 0;
}

/*
 * executeCommand() - Execute a filter
 * 
 * It takes input from inPipe fd and writes to outPipe fd.
 * 
 * Return - 
 * -1 - Error
 * pid of the error - Success
 */
static pid_t executeCommand(int inPipe,int outPipe,filter_t *filter,int i)
{
  pid_t pid;
  char *filename=filter->filter;
  fprintf(stderr,"Executing: %s\n",filename);
  fprintf(sout,"Filenmae: %s\n",filename);
  if((pid=fork())<0)
  {
    return -1;
  }
  else if(pid==0)
  {
    dup2(inPipe,0);
    dup2(outPipe,1);
    close(inPipe);
    close(outPipe);
    char *argv[10];

    int uid = getUserId(getenv("IPP_JOB_ORIGINATING_USER_NAME"));
    char userid[64];
    snprintf(userid,sizeof(userid),"%d",(uid<0?1000:uid));

    argv[0]=strdup(filename);
    argv[1]=strdup(getenv("IPP_JOB_ID"));// Job id
    argv[2]=strdup(userid);             // Userid
    argv[3]=strdup(getenv("IPP_JOB_NAME"));// Title
    argv[4]=strdup(getenv("IPP_COPIES_DEFAULT"));//Copies
    argv[5]=strdup("\"\"");             // Options
    argv[6]=NULL;
    char newpath[1024];
    setenv("CUPS_SERVERBIN",CUPS_SERVERBIN,1);
    setenv("OUTFORMAT",filter->dest->typename,1);
    execvp(*argv,argv);
    fflush(stderr);
    exit(0);
  }
  else{
    close(inPipe);
    close(outPipe);
    return pid;
  }
  exit(0);
}

/*
 * applyFilterChain() - Apply a series of filters given by *filters.
 * 
 * The input file name is given in inputFile.
 * Output File will be written to finalFile.
 * namelen - length of finalFile container.
 * 
 * Returns:
 * 0 - Success
 * !0 - Error 
 */
static int applyFilterChain(cups_array_t* filters,char *inputFile,char *finalFile,int namelen)
{
  int numPipes = cupsArrayCount(filters)+1;
  char outName[1024];
  int pipes[2*MAX_PIPES];
  int killall=0;
  int res = 0;
  pid_t pd;
  int status;

  cups_array_t* children=cupsArrayNew(NULL,NULL);

  if(children==NULL)
  {
    fprintf(stderr,"Out of memory!\n");
    return -1;
  }

  if(numPipes>MAX_PIPES)
  {
    fprintf(stderr,"ERROR: Too many Filters!\n");
    return -1;
  }

  snprintf(outName,sizeof(outName),"%s/printjob.XXXXXX",TMPDIR);

  for(int i=0;i<numPipes;i++)  
  {
    res = pipe(pipes+2*i);  /* Try Opening Pipes */
    if(res)                 /* Unable to open Pipes! */
      return -1;            
  }
  
  int inputFd = open(inputFile,O_RDONLY); /* Open Input file */
  if(inputFd<0)
  {
    if(errno == EACCES)
      fprintf(stderr,"ERROR: Permission Denied! Unable to open file: %s\n",inputFile);
    else
      fprintf(stderr,"ERROR: ERRNO: %d Unable to open file: %s\n",errno,inputFile);
    return -1;
  }

  int outputFd = mkstemp(outName);
  if(outputFd<0)
  {
    fprintf(stderr,"ERROR: ");
    if(errno==EACCES)
      fprintf(stderr,"Permission Denied!");
    if(errno==EEXIST)
      fprintf(stderr,"Directory is full! Used all temporary file names! ");
    fprintf(stderr,"Unable to open temporary file!\n");
    return -1;
  }

  strncpy(finalFile,outName,namelen);   /* Write temp filename to finalFile */

  dup2(inputFd,pipes[0]);     /* Input file */
  close(inputFd);
  
  dup2(outputFd,pipes[2*numPipes-1]);  /* Output(temp file) */
  close(outputFd);
  
  for(int i=0;i<numPipes-1;i++)
  {
    filter_t *tempFilter = ((filter_t*)cupsArrayIndex(filters,i));
    pid_t *pd=calloc(1,sizeof(pid_t));
    *pd = executeCommand(pipes[2*i],pipes[2*i+3],
                tempFilter,i);  /* Execute the printer */
    if(pd<0)
    {
      fprintf(stderr,"ERROR: Unable to execute filter %s!\n",tempFilter->filter);
      killall=1;  /* Chain failed kill all filters */
      goto error;
    }
    cupsArrayAdd(children,pd);
  }
  for(int i=0;i<numPipes;i++)    /* Close all pipes! */
  {
    close(pipes[2*i]);
    close(pipes[2*i+1]);
  }

  while(pd=waitpid(-1,&status,0)>0){    /* Wait for all child processes to exit. */
    if(WIFEXITED(status))
    {
      int es = WEXITSTATUS(status);
      fprintf(stderr,"%d Exited with status %d\n",pd,es); 
      if(es){
        killall=1;    /* (Atleast) One filter failed. kill entire chain. */
        goto error;
      }
    }
  }

  fprintf(sout,"DEBUG: Applied Filter Chain!\n");

error:
  if(killall){
    pid_t *temPid;
    for(temPid=cupsArrayFirst(children);temPid;temPid=cupsArrayNext(children))
    {
      kill(*temPid,SIGTERM);
    }
    return -1;
  }
  return 0;
}

/*
 * getDeviceScheme() - Get scheme(backend) from device_uri env variable.
 * 
 * It writes device_uri to device_uri_out.
 * It writes scheme to scheme.
 * 
 * Returns:
 * 
 * 0 - Success
 * !0 - Error
 */
static int getDeviceScheme(char **device_uri_out, char *scheme,int schemelen)
{
  // char *device_uri=strdup(getenv("DEVICE_URI"));
  char *device_uri=strdup("\"hp:/usb/OfficeJet_Pro_6960?serial=TH6CL621KN\"");
  int i;
	char userpass[256],		/* username:password (unused) */
		host[256],		/* Hostname or IP address */
		resource[256];		/* Resource path */
    int	port;			/* Port number */

  if(device_uri==NULL)
  {
    *device_uri_out=NULL;
    scheme=NULL;
    return -1;
  }
  device_uri[strlen(device_uri)-1]=0;     /* Remove last \" */

  for(i=0;i<strlen(device_uri);i++)       /* Remove first \" */
  {
    device_uri[i]=device_uri[i+1];
  }

  *device_uri_out=device_uri;

  if (httpSeparateURI(HTTP_URI_CODING_ALL,device_uri, scheme, schemelen, 
    userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
  {
    fprintf(stderr, "[Job %d] Bad device URI \"%s\".\n", 0, device_uri);
    *device_uri_out=NULL;
    return -1;
  }

  return 0;
}


static int print_document(char *scheme,char *uri, char *filename)
{
  char backend[2048];
  pid_t pid;
  int status;

  snprintf(backend,sizeof(backend),"%s/backend/%s",SERVERBIN,scheme);
  fprintf(sout,"Backend: %s %s\n",backend,uri);
  
  /*
   * Check file permissions and do fileCheck().
   */
  if((access(backend,F_OK|X_OK)==-1)&&fileCheck(backend))
  {
    fprintf(sout,"ERROR: Unable to execute backend %s\n",scheme);
    return -1;
  }
  
  dup2(fileno(sout),2); /* stderr-> File logs */
  
  if((pid=fork())<0)
  {
    fprintf(stderr,"Unable to fork!\n");
    return -1;
  }
  else if(pid==0)
  { 
    char userid[64];
    int uid;
    uid = getUserId(getenv("IPP_JOB_ORIGINATING_USER_NAME"));
    snprintf(userid,sizeof(userid),"%d",(uid<0?1000:uid));

    fprintf(stderr,"%s %s %s %s %s %s\n",uri,getenv("IPP_JOB_ID"),userid,
                          getenv("IPP_JOB_NAME"),getenv("IPP_COPIES_DEFAULT"),filename);
    char *argv[10];
    argv[0]=strdup(uri);
    argv[1]=strdup(getenv("IPP_JOB_ID"));// Job id
    argv[2]=strdup(userid);             // Userid
    argv[3]=strdup(getenv("IPP_JOB_NAME"));// Title
    argv[4]=strdup(getenv("IPP_COPIES_DEFAULT"));//Copies
    argv[5]=strdup("\"\"");             // Options
    argv[6]=strdup(filename);
    argv[7]=NULL;
    
    execvp(backend,argv);
  }
  

  while((pid=waitpid(-1,&status,0))>0)
  {
    if(WIFEXITED(status))
    {
      int er = WEXITSTATUS(status);
      fprintf(sout,"%s: Process %d exited with status %d\n",(er?"ERROR":"DEBUG"),pid,er);
      return er;
    }
  }
  return 0;
}

void testApplyFilterChain()
{
  cups_array_t* t = cupsArrayNew(NULL,NULL);
  cupsArrayAdd(t,"1");
  cupsArrayAdd(t,"1");
  cupsArrayAdd(t,"1");
  cupsArrayAdd(t,"1");
  char inputFile[1024];
  snprintf(inputFile,sizeof(inputFile),"%s/logs.txt",TMPDIR);
  // char *outFile;
  applyFilterChain(t,inputFile,NULL,0);
}

int main(int argc, char *argv[])
{
  ini();

  char device_scheme[32],*device_uri;
  char *ppdname=NULL;
  char *output_type=NULL;
  char *content_type=NULL;
  char finalFile[1024];

  getDeviceScheme(&device_uri,device_scheme,sizeof(device_scheme));
  fprintf(stderr,"DEBUG: Device_scheme: %s %s\n",device_scheme,device_uri);
  
  char **s = environ;
  int isPPD = 1,isOut=1;    
  for(;*s;){
      fprintf(sout,"%s\n",*s);
      s = (s+1);
  }
  
  if(argc!=2)
  {
    return -1;
  }
  // fclose(sout);
  // exit(0);
  char *inputFile=strdup(argv[1]); /* Input File */

  // if(getenv("CONTENT_TYPE")==NULL){
  //   exit(0);
  // }
  // if(getenv("PPD")==NULL){
  //   isPPD = 0;
  // }
  // if(getenv("OUTPUT_TYPE")==NULL){
  //   isOut =0;
  // }

  ppdname=strdup("/home/dj/Desktop/HP-OfficeJet-Pro-6960.ppd");
  setenv("PPD",ppdname,1);
  content_type=strdup("application/pdf");

  setenv("IPP_JOB_ORIGINATING_USER_NAME","dj",1);
  setenv("IPP_JOB_ID","1",1);
  setenv("IPP_JOB_NAME","test",1);
  setenv("IPP_COPIES_DEFAULT","1",1);
  setenv("PRINTER","HP Officejet 6960",1);
  
  // if(isPPD) 
  //   ppdname = strdup(getenv("PPD"));
  // content_type = strdup(getenv("CONTENT_TYPE"));
  // if(isOut)
  //   output_type = strdup(getenv("OUTPUT_TYPE"));

  cups_array_t *filter_chain,*filterfullname;
  filter_t *paths;
  filter_t *tempFilter;

  int res = get_ppd_filter_chain(content_type,output_type,ppdname,&filter_chain);

  if(res<0)
  {
    fprintf(stderr,"ERROR: Unable to find required filters");
    exit(-1);
  }

  for(tempFilter=cupsArrayFirst(filter_chain);tempFilter;
    tempFilter=cupsArrayNext(filter_chain))
  {
    fprintf(sout,"Filter: %s\n",tempFilter->filter);
  }
  
  res = getFilterPaths(filter_chain,&filterfullname);
  if(res<0)
  {
    fprintf(stderr,"ERROR: Unable to find required filters!\n");
    exit(-1);
  }
  for(paths=cupsArrayFirst(filterfullname);paths;
    paths=cupsArrayNext(filterfullname))
  {
    fprintf(sout,"Filter fn: %s\n",paths->filter);
  }
  fflush(sout);
  res = applyFilterChain(filterfullname,inputFile,finalFile,sizeof(finalFile));
  // fprintf(sout,"Final File Name: %s\n",finalFile);
  
  if(res<0)
  {
    fprintf(stderr,"ERROR: Filter Chain Error!\n");
    exit(-1);
  }

  if(device_uri)
  {
    res = print_document(device_scheme,device_uri,finalFile);
    if(res==0)
    {
      fprintf(sout,"DEBUG: Successfully printed file!\n");
    }
  }

  fprintf(sout,"*****************************************************\n");
  fclose(sout);
  return res;
}