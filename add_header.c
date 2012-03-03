/*
  add_header.c
  simple add_header(to origin server) remap plugin for Apache Traffic Server 3.0.0+

  this plugin will add the header only when the incomming request doesn't have
  duplicate header by default, and you can config to remove duplicate and add new one.

  Created by wkl <buaawkl@gmail.com> Sep 2011
*/
#include <stdio.h>
#include <string.h>

#include <ts/ts.h>
#include <ts/remap.h>

#define PLUGIN_NAME "add_header"
#define PLUGIN_VERSION "0.2"

typedef struct {
  TSMBuffer hdr_bufp;
  TSMLoc hdr_loc;
  char *key;
  char *value;
  int remove_duplicate; 
  /* remove existent header with same key (if exist many, remove first one)*/
} remap_config;

static TSTextLogObject log;


static void
handle_request(TSHttpTxn txnp, TSRemapRequestInfo* rri, remap_config* conf)
{
  TSMBuffer bufp = rri->requestBufp;
  TSMLoc hdr_loc = rri->requestHdrp;

  TSMLoc exist_field_loc, our_field_loc, field_loc_to_append;
  exist_field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, conf->key, -1);

  if (exist_field_loc) {
    TSDebug(PLUGIN_NAME, "already exist header '%s', we will %s.", conf->key,
      (conf->remove_duplicate ? "remove it and add new one" : "not add")
    );
    if (conf->remove_duplicate) {
      TSMimeHdrFieldDestroy(bufp, hdr_loc, exist_field_loc);
      TSHandleMLocRelease(bufp, hdr_loc, exist_field_loc);
    } else {
      TSHandleMLocRelease(bufp, hdr_loc, exist_field_loc);
      return;
    }
  }

  our_field_loc = TSMimeHdrFieldGet(conf->hdr_bufp, conf->hdr_loc, 0);
  if (our_field_loc == TS_NULL_MLOC) {
    TSError("[%s] Error while getting field", PLUGIN_NAME);
    return;
  }

  if (TSMimeHdrFieldCreate(bufp, hdr_loc, &field_loc_to_append) != TS_SUCCESS) {
    TSError("[%s] Error while creating new field", PLUGIN_NAME);
    TSHandleMLocRelease(conf->hdr_bufp, conf->hdr_loc, our_field_loc);
    return;
  }

  if (TS_SUCCESS != TSMimeHdrFieldCopy(bufp, hdr_loc, field_loc_to_append,
                      conf->hdr_bufp, conf->hdr_loc, our_field_loc)) {
    TSError("[%s] Error while copying field", PLUGIN_NAME);
    TSHandleMLocRelease(conf->hdr_bufp, conf->hdr_loc, our_field_loc);
    return;
  }

  if (TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc_to_append) != TS_SUCCESS) {
    TSError("[%s] can not append mime field to header\n", PLUGIN_NAME);
  } else {
    TSDebug(PLUGIN_NAME, "append success, %s:%s", conf->key, conf->value);
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc_to_append);
  TSHandleMLocRelease(conf->hdr_bufp, conf->hdr_loc, our_field_loc);
}

/*
  This function will be called for each request in the channel.
*/
TSRemapStatus
TSRemapDoRemap(void* ih, TSHttpTxn rh, TSRemapRequestInfo* rri)
{
  remap_config* conf = ih;

  handle_request(rh, rri, conf);

  return TSREMAP_NO_REMAP;  /* Continue with next remap plugin in chain */
}

/*
 create remap instance for each remap rule
*/
TSReturnCode
TSRemapNewInstance(int argc, char* argv[], void** ih, char* errbuf, 
                   int errbuf_size)
{
  if (argc < 3) {
    TSError("Unable to create remap instance, need 'key:value' param");
    TSDebug(PLUGIN_NAME, 
        "Unable to create remap instance, need 'key:value' param");
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "@pparam: %s", argv[2]);

  remap_config* conf = TSmalloc(sizeof(remap_config));
  TSMLoc field_loc;

  char *colon_pos = strchr(argv[2], ':');
  int key_len = 0;
  int value_len = 0;

  if (colon_pos) {
    key_len = colon_pos - argv[2];
    value_len = strlen(colon_pos) - 1;
  }

  if (key_len == 0 ||  value_len == 0) {

    TSError("Unable to create remap instance, need valid 'key:value' param");
    TSDebug(PLUGIN_NAME, 
        "Unable to create remap instance, need valid 'key:value' pparam, "
        "check your remap.config for '%s'", argv[0]);
    if (log) {
      TSTextLogObjectWrite(log, 
          "Unable to create remap instance, need valid 'key:value' param, "
          "check your remap.config for '%s'", argv[0]);
    }

    return TS_ERROR;
  }

  conf->key = TSmalloc(key_len + 1);
  conf->value = TSmalloc(value_len + 1);
  strncpy(conf->key, argv[2], key_len);
  conf->key[key_len] = '\0';
  strncpy(conf->value, colon_pos + 1, value_len);
  conf->value[value_len] = '\0';

  conf->hdr_bufp = TSMBufferCreate();

  if (TSMimeHdrCreate(conf->hdr_bufp, &conf->hdr_loc) != TS_SUCCESS) {
    TSError("[%s] can not create mime header", PLUGIN_NAME);
    return TS_ERROR;
  }

  /* create the header */
  if (TS_SUCCESS != TSMimeHdrFieldCreateNamed(conf->hdr_bufp, conf->hdr_loc, 
                      conf->key, key_len, &field_loc)) {
    TSError("[%s] can not create mime field\n", PLUGIN_NAME);
    return TS_ERROR;
  }

  if (TS_SUCCESS != TSMimeHdrFieldValueStringInsert(conf->hdr_bufp, 
                      conf->hdr_loc, field_loc, -1, conf->value, -1)) {
    TSError("[%s] can not set mime field value\n", PLUGIN_NAME);
    TSHandleMLocRelease(conf->hdr_bufp, conf->hdr_loc, field_loc);
    return TS_ERROR;
  }

  if (TS_SUCCESS != TSMimeHdrFieldAppend(conf->hdr_bufp, conf->hdr_loc, 
                      field_loc)) {
    TSError("[%s] can not append mime field to header\n", PLUGIN_NAME);
    TSHandleMLocRelease(conf->hdr_bufp, conf->hdr_loc, field_loc);
    return TS_ERROR;
  }

  TSHandleMLocRelease(conf->hdr_bufp, conf->hdr_loc, field_loc);

  /* check if remove exist header */
  conf->remove_duplicate = 0;
  if (argc == 4) {
    if (strcmp(argv[3], "remove_duplicate") == 0) {
      conf->remove_duplicate = 1;
    }
  }

  *ih = (void*)conf;

  /* log it */
  if (log) {
    TSTextLogObjectWrite(log, 
      "%s Remap Instance created for '%s', header value pair: '%s' -> '%s'"
      ", will %s.", PLUGIN_NAME, argv[0], conf->key, conf->value, 
      (conf->remove_duplicate ? "remove duplicate" : "not add if exist duplicate")
    );
  }
  TSDebug(PLUGIN_NAME, "%s Remap Instance created for '%s', header value pair:"
    "'%s' -> '%s', will %s.", PLUGIN_NAME, argv[0], conf->key, conf->value,
    (conf->remove_duplicate ? "remove duplicate" : "not add if exist duplicate")
  );

  return TS_SUCCESS;
}

/* Release instance memory allocated in TSRemapNewInstance */
void
TSRemapDeleteInstance(void* ih)
{
  TSDebug(PLUGIN_NAME, "deleting remap instance %p", ih);

  if (!ih) return;

  remap_config* conf = ih;
  if (conf) {
    if (conf->hdr_loc) {
      TSHandleMLocRelease(conf->hdr_bufp, TS_NULL_MLOC, conf->hdr_loc);
    }
    if (conf->hdr_bufp) TSMBufferDestroy(conf->hdr_bufp);
    if (conf->key) TSfree(conf->key);
    if (conf->value) TSfree(conf->value);
  }

  TSfree(conf);
}

/* Function to ensure we're running a recent enough version of Traffic Server.
 * (Taken from the example plugin)
 */
static int
check_ts_version() 
{
  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0; 
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version,
                &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 3.0.0 */
    if (major_ts_version >= 3) {
      result = 1;
    }

  }
  return result;
}

/*
  Initalize the plugin as a remap plugin.
*/
TSReturnCode 
TSRemapInit(TSRemapInterface* api_info, char* errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[TSRemapInit] - Invalid TSRemapInterface argument", 
            errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, 
            "[TSRemapInit] - Incorrect size of TSRemapInterface structure", 
            errbuf_size - 1);
    return TS_ERROR;
  }

  TSReturnCode error;
  if (!log) {
    error = TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, &log);
    if (!log || error == TS_ERROR) {
      TSError("[%s] Error creating log file\n", PLUGIN_NAME);
    }
  }

  if (!check_ts_version()) {
    TSError("[%s] Plugin requires Traffic Server 3.0.0 or later", PLUGIN_NAME);
  }

  TSDebug(PLUGIN_NAME, "%s plugin is initialized, version: %s",
          PLUGIN_NAME, PLUGIN_VERSION);

  if (log) {
    TSTextLogObjectWrite(log, "%s plugin is initialized, version: %s",
        PLUGIN_NAME, PLUGIN_VERSION);
  }

  return TS_SUCCESS;
}

