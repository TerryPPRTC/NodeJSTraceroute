#include <stdio.h>
#include <stdlib.h>
#include <node_api.h>
#include <assert.h>

// Limit ourselves to this many primes, starting at 2
#define PRIME_COUNT 10
// #define CHECK(expr) \
//   { \
//     if ((expr) == 0) { \
//       fprintf(stderr, "%s:%d: failed assertion `%s'\n", __FILE__, __LINE__, #expr); \
//       fflush(stderr); \
//       abort(); \
//     } \
//   }

#define CHECK_WITH_RET(expr, ret) \
  { \
    if((expr) == 0) { \
      return ret; \
    } \
  }

#define CHECK(expr) \
  { \
    if((expr) == 0) { \
      return; \
    } \
  }

typedef struct linked_box_s linked_box_t;
struct linked_box_s {
  linked_box_t* prev;
  int the_prime;
};

typedef struct {
  napi_async_work work;
  napi_deferred deferred;
  char ip_data[64];
  char* p_result;
} AddonData;


extern char* main_start(const char* dest_ip);
// This function runs on a worker thread. It has no access to the JavaScript.
static void ExecuteWork(napi_env env, void* data) {
  AddonData* addon_data = (AddonData*)data;
  if(!addon_data)
    return;
  addon_data->p_result = NULL;

  char* p = main_start(addon_data->ip_data);
  if(p){
    addon_data->p_result = p;
  }
}

// This function runs on the main thread after `ExecuteWork` exits.
static void WorkComplete(napi_env env, napi_status status, void* data) {
  if (status != napi_ok) {
    return;
  }

  AddonData* addon_data = (AddonData*)data;
  if(!addon_data || !addon_data->p_result)
    return;

  napi_value retJson; 
  if(napi_ok != napi_create_string_utf8(env, addon_data->p_result, strlen(addon_data->p_result), &retJson)){
    free(addon_data->p_result);
    return;
  }

  if(napi_resolve_deferred(env, addon_data->deferred, retJson) != napi_ok){
    free(addon_data->p_result);
    return;
  }

  free(addon_data->p_result);
  addon_data->p_result = NULL;

  CHECK(napi_delete_async_work(env, addon_data->work) == napi_ok);

  // Set both values to NULL so JavaScript can order a new run of the thread.
  addon_data->work = NULL;
  addon_data->deferred = NULL;
}

// Create a deferred promise and an async queue work item.
static napi_value StartWork(napi_env env, napi_callback_info info) {
  napi_value work_name, promise;
  AddonData* addon_data;

  napi_value udefv;
  napi_get_undefined(env, &udefv);

  size_t argc = 1;
  napi_value args[1];
  napi_status status = napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  assert(status == napi_ok);

  napi_valuetype valuetype0;
  status = napi_typeof(env, args[0], &valuetype0);
  assert(status == napi_ok);

  if (valuetype0 != napi_string) {
    napi_throw_type_error(env, NULL, "Wrong arguments");
    return NULL;
  }

  char ip_buf[64] = {0};
  size_t result;
  status = napi_get_value_string_utf8(env, args[0], ip_buf, 64, &result);
  assert(status == napi_ok);

  // Retrieve the per-addon data.
  CHECK_WITH_RET(napi_get_cb_info(env,
                          info,
                          NULL,
                          NULL,
                          NULL,
                          (void**)(&addon_data)) == napi_ok, udefv);

  memcpy(addon_data->ip_data, ip_buf, 64);

  // Ensure that no work is currently in progress.
  CHECK_WITH_RET(addon_data->work == NULL && "Only one work item must exist at a time", udefv);

  // Create a string to describe this asynchronous operation.
  CHECK_WITH_RET(napi_create_string_utf8(env,
                                 "N-API Deferred Promise from Async Work Item",
                                 NAPI_AUTO_LENGTH,
                                 &work_name) == napi_ok, udefv);

  // Create a deferred promise which we will resolve at the completion of the work.
  CHECK_WITH_RET(napi_create_promise(env,
                             &(addon_data->deferred),
                             &promise) == napi_ok, udefv);

  // Create an async work item, passing in the addon data, which will give the
  // worker thread access to the above-created deferred promise.
  CHECK_WITH_RET(napi_create_async_work(env,
                                NULL,
                                work_name,
                                ExecuteWork,
                                WorkComplete,
                                addon_data,
                                &(addon_data->work)) == napi_ok, udefv);

  // Queue the work item for execution.
  CHECK_WITH_RET(napi_queue_async_work(env, addon_data->work) == napi_ok, udefv);

  // This causes created `promise` to be returned to JavaScript.
  return promise;
}

// Free the per-addon-instance data.
static void addon_getting_unloaded(napi_env env, void* data, void* hint) {
  AddonData* addon_data = (AddonData*)data;
  CHECK(addon_data->work == NULL &&
      "No work item in progress at module unload");
  free(addon_data);
}

// The commented-out return type and the commented out formal function
// parameters below help us keep in mind the signature of the addon
// initialization function. We write the body as though the return value were as
// commented below and as though there were parameters passed in as commented
// below.
/*napi_value*/ NAPI_MODULE_INIT(/*napi_env env, napi_value exports*/) {

  // Define addon-level data associated with this instance of the addon.
  AddonData* addon_data = (AddonData*)malloc(sizeof(*addon_data));
  addon_data->work = NULL;

  // Define the properties that will be set on exports.
  napi_property_descriptor start_work = {
    "startWork",
    NULL,
    StartWork,
    NULL,
    NULL,
    NULL,
    napi_default,
    addon_data
  };

  // Decorate exports with the above-defined properties.
  CHECK_WITH_RET(napi_define_properties(env, exports, 1, &start_work) == napi_ok, NULL);

  // Associate the addon data with the exports object, to make sure that when
  // the addon gets unloaded our data gets freed.
  CHECK_WITH_RET(napi_wrap(env,
                   exports,
                   addon_data,
                   addon_getting_unloaded,
                   NULL,
                   NULL) == napi_ok, NULL);

  // Return the decorated exports object.
  return exports;
}

