#include "httpd.h"
#include "http_protocol.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

// Global JVM instance
JavaVM *jvm;
JNIEnv *env;

// Function to initialize the JVM
int initialize_jvm(const char *jar_path) {
    JavaVMInitArgs vm_args;
    JavaVMOption options[1];

    // Construct the classpath option, including the path to the JAR file
    char classpath_option[1024];
    snprintf(classpath_option, sizeof(classpath_option), "-Djava.class.path=%s", jar_path);

    // Set the JVM options (classpath in this case)
    options[0].optionString = classpath_option;
    vm_args.version = JNI_VERSION_1_6;
    vm_args.nOptions = 1;
    vm_args.options = options;
    vm_args.ignoreUnrecognized = JNI_FALSE;

    // Initialize the JVM
    jint res = JNI_CreateJavaVM(&jvm, (void **)&env, &vm_args);
    if (res < 0) {
        fprintf(stderr, "Failed to create JVM\n");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    return OK;
}

// Function to invoke the Java class
int invoke_java_class(request_rec *r, const char *jar_path, const char *document_root, const char *file_path) {
    initialize_jvm(jar_path);

    // Find the main class (you can change this class name to match the one in your JAR)
    jclass cls = (*env)->FindClass(env, "ApacheHandler");
    if (cls == NULL) {
        (*env)->ExceptionDescribe(env);
        (*jvm)->DestroyJavaVM(jvm);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Find the main method of the class
    jmethodID mid = (*env)->GetStaticMethodID(env, cls, "main", "([Ljava/lang/String;)V");
    if (mid == NULL) {
        (*env)->ExceptionDescribe(env);
        (*jvm)->DestroyJavaVM(jvm);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    // Create an empty String array to pass as arguments to the main method
    //jobjectArray args = (*env)->NewObjectArray(env, 0, (*env)->FindClass(env, "java/lang/String"), NULL);
    jobjectArray args = (*env)->NewObjectArray(env, 2, (*env)->FindClass(env, "java/lang/String"), NULL);
    jstring jstr_document_root = (*env)->NewStringUTF(env, document_root);
    jstring jstr_file_path = (*env)->NewStringUTF(env, file_path);
    (*env)->SetObjectArrayElement(env, args, 0, jstr_document_root);  // Set DocumentRoot
    (*env)->SetObjectArrayElement(env, args, 1, jstr_file_path);

    // Call the main method
    (*env)->CallStaticVoidMethod(env, cls, mid, args);

    // Destroy the JVM
    (*jvm)->DestroyJavaVM(jvm);

    return OK;
}

// Handler function for Apache requests
static int zpe_handler(request_rec *r) {
    if (strcmp(r->handler, "zpe")) {
        return DECLINED;
    }
    r->content_type = "text/html";

    // Check if the client request is a GET request
    if (r->method_number != M_GET) {
        return HTTP_METHOD_NOT_ALLOWED;
    }

    // Retrieve the Apache DocumentRoot from the server configuration
    const char *document_root = ap_document_root(r);

    // The path to the JAR file (you can modify this or pass it as a parameter)
    const char *jar_path = "/zpe/zpe.jar";

    /*
    // Invoke the Java class
    int result = invoke_java_class(r, jar_path);

    // Send a response back to the client
    ap_rputs("Java application executed.\n", r);
    return result;*/

    // Redirect stdout to capture Java output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        ap_rputs("Failed to create pipe for capturing Java output.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    int stdout_copy = dup(STDOUT_FILENO); // Save a copy of stdout
    int stderr_copy = dup(STDERR_FILENO); // Save a copy of stderr

    // Redirect stdout and stderr to the pipe
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    const char *file_path = r->uri;

    // Invoke the Java class
    int result = invoke_java_class(r, jar_path, document_root, file_path);

    // Restore stdout and stderr
    dup2(stdout_copy, STDOUT_FILENO);
    dup2(stderr_copy, STDERR_FILENO);
    close(stdout_copy);
    close(stderr_copy);

    if (result != OK) {
        return result; // Return the error code if invocation failed
    }

    // Read the captured output from the pipe
    char buffer[4096];
    ssize_t nbytes;
    close(pipefd[1]); // Close write end, as it's no longer needed

    while ((nbytes = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[nbytes] = '\0'; // Null-terminate the buffer
        ap_rputs(buffer, r); // Send the output to the client
    }

    close(pipefd[0]);

    return OK;
}

// Apache module registration
static void register_hooks(apr_pool_t *pool) {
    ap_hook_handler(zpe_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

// Declare the module
module AP_MODULE_DECLARE_DATA zpe_module = {
    STANDARD20_MODULE_STUFF,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    register_hooks
};
