#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "cros_service.h"
#include "cros_service_internal.h"
#include "cros_message_internal.h"
#include "tcpros_tags.h"
#include "cros_defs.h"
#include "md5.h"

cRosErrCodePack initCrosSrv(cRosSrvDef* srv)
{
    cRosErrCodePack ret_err;

    if(srv == NULL)
        return CROS_BAD_PARAM_ERR;

    srv->request = (cRosMessageDef*) malloc(sizeof(cRosMessageDef));
    srv->response = (cRosMessageDef*) malloc(sizeof(cRosMessageDef));
    if(srv->request != NULL && srv->response != NULL)
    {
        ret_err = initCrosMsg(srv->request);
        if(ret_err == CROS_SUCCESS_ERR_PACK)
        {
            ret_err = initCrosMsg(srv->response);
            if(ret_err == CROS_SUCCESS_ERR_PACK)
            {
                srv->name = NULL;
                srv->package = NULL;
                srv->plain_text = NULL;
                srv->root_dir = NULL;
                ret_err = CROS_SUCCESS_ERR_PACK;
            }
            else
            {
                cRosMessageDefFree(srv->request);
                free(srv->request);
                free(srv->response);
                ret_err = CROS_MEM_ALLOC_ERR;
            }
        }
        else
        {
            free(srv->request);
            free(srv->response);
            ret_err = CROS_MEM_ALLOC_ERR;
        }
    }
    else
    {
        free(srv->request);
        free(srv->response);
        ret_err = CROS_MEM_ALLOC_ERR;
    }
    return ret_err;
}

cRosErrCodePack loadFromFileSrv(char* filename, cRosSrvDef* srv)
{
    cRosErrCodePack ret_err;

    char* file_tokenized = (char*) malloc(strlen(filename)+sizeof(char));
    if(file_tokenized == NULL)
        return CROS_MEM_ALLOC_ERR;

    file_tokenized[0] = '\0';
    strcpy(file_tokenized, filename);
    char* token_pack = NULL;
    char* token_root = NULL;
    char* token_name = NULL;

    FILE *f = fopen(filename, "rb");

    if(f != NULL)
    {
        ret_err=CROS_SUCCESS_ERR_PACK;

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *srv_req = NULL;
        char *srv_res = NULL;
        char *srv_text = malloc(fsize + 1);
        if(srv_text == NULL)
        {
            fclose(f);
            free(file_tokenized);
            return CROS_MEM_ALLOC_ERR;
        }

        fread(srv_text, fsize, 1, f);
        fclose(f);

        srv_text[fsize] = '\0';
        srv->plain_text = strdup(srv_text); // equiv. to malloc() + memcpy()
        if(srv->plain_text == NULL)
        {
            free(srv_text);
            free(file_tokenized);
            return CROS_MEM_ALLOC_ERR;
        }

        //splitting msg_text into the request response parts
        srv_res = strstr(srv_text, SRV_DELIMITER);
        if(srv_res == NULL)
        {
            free(srv_text);
            free(file_tokenized);
            return CROS_SVC_FILE_DELIM_ERR;
        }

        //if the srv has some request parameters
        if(srv_res != srv_text)
        {
          //split before the first delim char
          *(srv_res - 1) = '\0';
          srv_req = srv_text;
        }

        srv_res += strlen(SRV_DELIMITER); // skip the delimiter
        if(*srv_res == '\n')
          srv_res++; // skip the new line char

        char* tok = strtok(file_tokenized,"/.");

        while(tok != NULL)
        {
            if(strcmp(tok, "srv") != 0)
            {
                token_root = token_pack;
                token_pack = token_name;
                token_name = tok ;
            }
            tok = strtok(NULL,"/.");
        }

        //build up the root path
        char* it = file_tokenized;
        while(it != token_root)
        {
            if(*it == '\0')
                *it='/';
            it++;
        }

        srv->root_dir = (char*) malloc (strlen(file_tokenized)+sizeof(char)); srv->root_dir[0] = '\0';
        srv->package = (char*) malloc (strlen(token_pack)+sizeof(char)); srv->package[0] = '\0';
        srv->name = (char*) malloc (strlen(token_name)+sizeof(char)); srv->name[0] = '\0';
        if(srv->root_dir != NULL && srv->package != NULL && srv->name != NULL)
        {
            strcpy(srv->root_dir,file_tokenized);
            strcpy(srv->package,token_pack);
            strcpy(srv->name,token_name);
        }
        else
          ret_err=CROS_MEM_ALLOC_ERR;

        if(srv_req != NULL)
        {
          srv->request->package = strdup(srv->package);
          srv->request->root_dir = strdup(srv->root_dir);
          if(srv->request->package != NULL && srv->request->root_dir != NULL)
          {
            ret_err=loadFromStringMsg(srv_req, srv->request);
            ret_err=cRosAddErrCodeIfErr(ret_err, CROS_LOAD_SVC_FILE_REQ_ERR); // If loadFromStringMsg() failed, add more info to the error-code pack, indicating the context
          }
          else
            ret_err=CROS_MEM_ALLOC_ERR;
        }

        if(strlen(srv_res) != 0)
        {
          srv->response->package = strdup(srv->package);
          srv->response->root_dir = strdup(srv->root_dir);
          if(srv->response->package != NULL && srv->response->root_dir != NULL)
          {
            ret_err=loadFromStringMsg(srv_res, srv->response);
            ret_err=cRosAddErrCodeIfErr(ret_err, CROS_LOAD_SVC_FILE_RES_ERR); // If loadFromStringMsg() failed, add more another error code to the to the error-code pack indicating the context
          }
          else
            ret_err=CROS_MEM_ALLOC_ERR;
        }

        if(ret_err != CROS_SUCCESS_ERR_PACK) // An error occurred, free allocated memory before exiting
        {
          free(srv->response->root_dir);
          free(srv->response->package);
          free(srv->request->root_dir);
          free(srv->request->package);
          free(srv->root_dir);
          free(srv->package);
          free(srv->name);
        }

        free(srv_text);
    }
    else
      ret_err=CROS_OPEN_SVC_FILE_ERR;

    free(file_tokenized);

    return ret_err;
}

//  Compute dependencies of the specified service file
cRosErrCodePack getFileDependenciesSrv(char* filename, cRosSrvDef* srv, msgDep* deps)
{
    cRosErrCodePack ret_err;
    ret_err = loadFromFileSrv(filename, srv);
    if(ret_err == CROS_SUCCESS_ERR_PACK)
    {
        ret_err = getDependenciesMsg(srv->request,deps);
        if(ret_err == CROS_SUCCESS_ERR_PACK)
            ret_err = getDependenciesMsg(srv->response,deps);
    }
    return ret_err;
}

//  Compute full text of service, including text of embedded
//  types.  The text of the main srv is listed first. Embedded
//  srv files are denoted first by an 80-character '=' separator,
//  followed by a type declaration line,'MSG: pkg/type', followed by
//  the text of the embedded type.
char* computeFullTextSrv(cRosSrvDef* srv, msgDep* deps)
{
    char* full_text = NULL;
    char* msg_tag = "MSG: ";
    int full_size = 0;
    char separator[81]; separator[80] = '\0';
    int i;
    memset(&separator,'=', 80);
    full_size += strlen(srv->plain_text);

    while(deps->next != NULL)
    {
        //printf("%s\nMSG: %s\n", separator, deps->msg->name);
        full_size = strlen(deps->msg->plain_text) + strlen(separator) + strlen(msg_tag) + 3/*New lines*/;
        deps = deps->next;
    }

    //rollback
    while(deps->prev != NULL) deps = deps->prev;
    full_text = (char*) malloc(full_size + 1);
    memcpy(full_text,srv->plain_text,strlen(srv->plain_text) + 1);

    while(deps->next != NULL)
    {
        //printf("%s\nMSG: %s\n", separator, deps->msg->name);
        strcat(full_text, "\n");
        strcat(full_text, separator);
        strcat(full_text, "\n");
        strcat(full_text, msg_tag);
        strcat(full_text, deps->msg->package);
        strcat(full_text, "/");
        strcat(full_text, deps->msg->name);
        strcat(full_text, "\n");
        strcat(full_text, deps->msg->plain_text);
        deps = deps->next;
    }
    return full_text;
}

cRosService * cRosServiceNew()
{
  cRosService *ret = (cRosService *)calloc(1, sizeof(cRosService));
  cRosServiceInit(ret);
  return ret;
}

void cRosServiceInit(cRosService* service)
{
  cRosMessageInit(&service->request);
  cRosMessageInit(&service->response);
  service->md5sum = (char*) malloc(33*sizeof(char));// 32 chars + '\0';
}

cRosErrCodePack cRosServiceBuild(cRosService* service, const char* filepath)
{
  return cRosServiceBuildInner(&service->request, &service->response, NULL, service->md5sum, filepath);
}

cRosErrCodePack cRosServiceBuildInner(cRosMessage *request, cRosMessage *response, char **message_definition, char *md5sum, const char* filepath)
{
  cRosErrCodePack ret_err;

  cRosSrvDef* srv = (cRosSrvDef*) malloc(sizeof(cRosSrvDef));
  if(srv == NULL)
    return CROS_MEM_ALLOC_ERR;

  ret_err = initCrosSrv(srv);
  if(ret_err != CROS_SUCCESS_ERR_PACK)
  {
    free(srv);
    return ret_err;
  }
  char* copy_filepath = malloc(strlen(filepath)+sizeof(char));
  if(copy_filepath == NULL)
  {
    cRosServiceDefFree(srv);
    return CROS_MEM_ALLOC_ERR;
  }
  *copy_filepath = '\0';
  strcpy(copy_filepath,filepath);

  ret_err = loadFromFileSrv(copy_filepath, srv);
  free(copy_filepath);
  if (ret_err != CROS_SUCCESS_ERR_PACK)
  {
    cRosServiceDefFree(srv);
    return ret_err;
  }

  unsigned char* res = NULL;
  DynString buffer;
  dynStringInit(&buffer);

  MD5_CTX md5_t;
  MD5_Init(&md5_t);

  if(srv->request->plain_text != NULL)
  {
    getMD5Txt(srv->request, &buffer);
    MD5_Update(&md5_t,buffer.data,buffer.len - 1);
    dynStringClear(&buffer);
  }

  if(srv->response->plain_text != NULL)
  {
    getMD5Txt(srv->response, &buffer);
    MD5_Update(&md5_t,buffer.data,buffer.len - 1);
  }
  dynStringRelease(&buffer);

  unsigned char* result = (unsigned char*) malloc(16);
  MD5_Final(result, &md5_t);
  DynString output;
  dynStringInit(&output);
  cRosMD5Readable(result,&output);
  free(result);

  strcpy(md5sum, output.data);
  dynStringRelease(&output);

  if(srv->request->plain_text != NULL)
  {
    ret_err = cRosMessageBuildFromDef(request, srv->request);
  }

  if(ret_err == CROS_SUCCESS_ERR_PACK && srv->response->plain_text != NULL)
  {
    ret_err = cRosMessageBuildFromDef(response, srv->response);
  }

  if(ret_err == CROS_SUCCESS_ERR_PACK && srv->plain_text != NULL && message_definition != NULL)
  {
     *message_definition=(char *)malloc((strlen(srv->plain_text)+1)*sizeof(char));
     if(*message_definition != NULL)
        strcpy(*message_definition,srv->plain_text);
     else
       ret_err = CROS_MEM_ALLOC_ERR;
  }

  cRosServiceDefFree(srv);
  return ret_err;
}

void cRosServiceDefFree(cRosSrvDef* service_def)
{
  if(service_def != NULL)
  {
    free(service_def->name);
    free(service_def->package);
    free(service_def->plain_text);
    free(service_def->root_dir);
    cRosMessageDefFree(service_def->request);
    cRosMessageDefFree(service_def->response);
    free(service_def);
  }
}

void cRosServiceFree(cRosService* service)
{
  cRosServiceRelease(service);
  free(service);
}

void cRosServiceRelease(cRosService* service)
{
  cRosMessageRelease(&service->request);
  cRosMessageRelease(&service->response);
  free(service->md5sum);
  service->md5sum = NULL;
}

static uint32_t getLen( DynBuffer *pkt )
{
  uint32_t len;
  const unsigned char *data = dynBufferGetCurrentData(pkt);
  ROS_TO_HOST_UINT32(*((uint32_t *)data), len);
  dynBufferMovePoseIndicator(pkt,sizeof(uint32_t));

  return len;
}

static uint32_t pushBackField( DynBuffer *pkt, TcprosTagStrDim *tag, const char *val )
{
  size_t val_len = strlen( val );
  uint32_t out_len, field_len = tag->dim + val_len;
  //PRINT_DEBUG("pushBackField() : filed : %s field_len ; %d\n", tag->str, field_len);
  HOST_TO_ROS_UINT32( field_len, out_len );
  dynBufferPushBackUInt32( pkt, out_len );
  dynBufferPushBackBuf( pkt, (const unsigned char*)tag->str, tag->dim );
  dynBufferPushBackBuf( pkt, (const unsigned char*)val, val_len );

  return field_len + sizeof( uint32_t );
}
