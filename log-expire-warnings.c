#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "openvpn-plugin.h"
#include "x509.h"

#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)

#define PLUGIN_NAME "log-expire-warnings"

/* Where we store our own settings/state */
struct plugin_context
{
    plugin_log_t plugin_log;
    const char *output_filename;
    int warn_days;
};

void show(const int type, const char *argv[], const char *envp[], const X509 *current_cert)
{
    size_t i;
    switch (type)
    {
    case OPENVPN_PLUGIN_UP:
        printf("OPENVPN_PLUGIN_UP\n");
        break;

    case OPENVPN_PLUGIN_DOWN:
        printf("OPENVPN_PLUGIN_DOWN\n");
        break;

    case OPENVPN_PLUGIN_ROUTE_UP:
        printf("OPENVPN_PLUGIN_ROUTE_UP\n");
        break;

    case OPENVPN_PLUGIN_IPCHANGE:
        printf("OPENVPN_PLUGIN_IPCHANGE\n");
        break;

    case OPENVPN_PLUGIN_TLS_VERIFY:
        printf("OPENVPN_PLUGIN_TLS_VERIFY\n");
        break;

    case OPENVPN_PLUGIN_AUTH_USER_PASS_VERIFY:
        printf("OPENVPN_PLUGIN_AUTH_USER_PASS_VERIFY\n");
        break;

    case OPENVPN_PLUGIN_CLIENT_CONNECT_V2:
        printf("OPENVPN_PLUGIN_CLIENT_CONNECT_V2\n");
        break;

    case OPENVPN_PLUGIN_CLIENT_DISCONNECT:
        printf("OPENVPN_PLUGIN_CLIENT_DISCONNECT\n");
        break;

    case OPENVPN_PLUGIN_LEARN_ADDRESS:
        printf("OPENVPN_PLUGIN_LEARN_ADDRESS\n");
        break;

    case OPENVPN_PLUGIN_TLS_FINAL:
        printf("OPENVPN_PLUGIN_TLS_FINAL\n");
        break;

    default:
        printf("OPENVPN_PLUGIN_?\n");
        break;
    }

    if (current_cert)
    {
        printf("Cert available\n");
    }
    else
    {
        printf("Cert not available\n");
    }

    printf("ARGV\n");
    for (i = 0; argv[i] != NULL; ++i)
    {
        printf("%d '%s'\n", (int)i, argv[i]);
    }

    printf("ENVP\n");
    for (i = 0; envp[i] != NULL; ++i)
    {
        printf("%d '%s'\n", (int)i, envp[i]);
    }
}

/*
 * Given an environmental variable name, search
 * the envp array for its value, returning it
 * if found or NULL otherwise.
 */
static const char *
get_env(const char *name, const char *envp[])
{
    if (envp)
    {
        int i;
        const int namelen = strlen(name);
        for (i = 0; envp[i]; ++i)
        {
            if (!strncmp(envp[i], name, namelen))
            {
                const char *cp = envp[i] + namelen;
                if (*cp == '=')
                {
                    return cp + 1;
                }
            }
        }
    }
    return NULL;
}

OPENVPN_EXPORT int
openvpn_plugin_open_v3(const int v3structver,
                       struct openvpn_plugin_args_open_in const *args,
                       struct openvpn_plugin_args_open_return *retptr)
{
    plugin_log_t log = args->callbacks->plugin_log;
    log(PLOG_DEBUG, PLUGIN_NAME, "FUNC: openvpn_plugin_open_v3");

    struct plugin_context *context = NULL;

    /* Safeguard on openvpn versions */
    if (v3structver < OPENVPN_PLUGINv3_STRUCTVER)
    {
        log(PLOG_ERR, PLUGIN_NAME,
            "ERROR: struct version was older than required");
        return OPENVPN_PLUGIN_FUNC_ERROR;
    }

    if (args->ssl_api != SSLAPI_OPENSSL)
    {
        log(PLOG_ERR, PLUGIN_NAME, "This plug-in can only be used against OpenVPN with OpenSSL");
        return OPENVPN_PLUGIN_FUNC_ERROR;
    }

    log(PLOG_NOTE, PLUGIN_NAME, "Version: [%s]", STRINGIZE_VALUE_OF(VERSION));
    log(PLOG_NOTE, PLUGIN_NAME, "Commit Hash: [%s]", STRINGIZE_VALUE_OF(COMMIT_HASH));
    log(PLOG_NOTE, PLUGIN_NAME, "Build Time: [%s]", STRINGIZE_VALUE_OF(BUILD_TIME));

    /*  Which callbacks to intercept.  */
    retptr->type_mask = OPENVPN_PLUGIN_MASK(OPENVPN_PLUGIN_TLS_VERIFY);

    /*
     *
     * Note that if arg_size is 0 no script argument was included.
     */
    if (!(args->argv[1] || args->argv[2]))
    {
        printf("ERROR: no output_filename or warn days specified in config file");
        return OPENVPN_PLUGIN_FUNC_ERROR;
    }

    /*
     * Plugin init will fail unless we create a handler, so we'll store our
     * script path and it's arguments there as we have to create it anyway.
     */
    context = (struct plugin_context *)malloc(
        sizeof(struct plugin_context) + strlen(args->argv[1]));
    memset(context, 0, sizeof(struct plugin_context));
    memcpy(&context->output_filename, &args->argv[1], strlen(args->argv[1]));

    context->plugin_log = log;
    context->warn_days = atoi(args->argv[2]);

    log(PLOG_DEBUG, PLUGIN_NAME,
        "output_filename=%s",
        context->output_filename);
    log(PLOG_DEBUG, PLUGIN_NAME,
        "warn_days=%i",
        context->warn_days);

    /* Pass state back to OpenVPN so we get handed it back later */
    retptr->handle = (openvpn_plugin_handle_t)context;

    log(PLOG_DEBUG, PLUGIN_NAME, "plugin initialized successfully");

    return OPENVPN_PLUGIN_FUNC_SUCCESS;
}

static void
notify_going_to_expire(ASN1_TIME *now_asn1time, const ASN1_TIME *not_after_time, const char *common_name, const char *filename)
{
    printf("notify_going_to_expire :::::>> CN: [%s] FN: [%s]\n", common_name, filename);
    BIO *outstd, *out;
    outstd = BIO_new_fp(stdout, BIO_NOCLOSE);
    out = BIO_new_file(filename, "a");
    if (!out)
    {
        BIO_printf(outstd, "Error opening file [%s]\n", filename);
    }
    if (!now_asn1time)
    {
        BIO_printf(outstd, "now_asn1time is null\n");
        return;
    }

    if (!not_after_time)
    {
        BIO_printf(outstd, "not_after_time is null\n");
        return;
    }

    if (!ASN1_TIME_print(out, now_asn1time))
    {
        BIO_printf(outstd, "Error writing time now_asn1time\n");
        return;
    }
    BIO_printf(out, ",%s,", common_name);
    if (!ASN1_TIME_print(out, not_after_time))
    {
        BIO_printf(outstd, "Error writing time not_after_time\n");
        return;
    }
    BIO_printf(out, "\n");

    BIO_free(out);
}

static void
x509_print_info(X509 *x509crt, const char *common_name, const char *filename, const int warn_days)
{
    ASN1_TIME *now_asn1time = NULL;
    int day, sec;

    printf("x509_print_info :::::>> CN: [%s] FN: [%s] d: [%i]\n", common_name, filename, warn_days);

    now_asn1time = ASN1_TIME_set(NULL, time(NULL));

    const ASN1_TIME *not_after_time = X509_get0_notAfter(x509crt);

    ASN1_TIME_diff(&day, &sec, now_asn1time, not_after_time);

    if (day > warn_days)
    {
        return;
    }
    else
    {
        // time difference less than warn_days so notify
        notify_going_to_expire(now_asn1time, not_after_time, common_name, filename);
    }

    ASN1_TIME_free(now_asn1time);
}

OPENVPN_EXPORT int
openvpn_plugin_func_v3(const int version,
                       struct openvpn_plugin_args_func_in const *args,
                       struct openvpn_plugin_args_func_return *retptr)
{
    struct plugin_context *context =
        (struct plugin_context *)args->handle;
    plugin_log_t log = context->plugin_log;

    printf("\nopenvpn_plugin_func_v3() :::::>> ");
    show(args->type, args->argv, args->envp, args->current_cert);
    printf("openvpn_plugin_func_v3 :::::>> %s\n", args->argv[2]);

    if ((args->type == OPENVPN_PLUGIN_TLS_VERIFY) && args->current_cert)
    {
        if (args->current_cert_depth == 0)
        {
            const char *common_name = get_env("CN", args->argv);
            const char *output_filename = context->output_filename;
            int warn_days = context->warn_days;
            log(PLOG_DEBUG, PLUGIN_NAME, "verify depth 0 :::::>> CN: [%s]\n", common_name);

            x509_print_info(args->current_cert, common_name, output_filename, warn_days);
        }
    }

    return OPENVPN_PLUGIN_FUNC_SUCCESS;
}

OPENVPN_EXPORT void
openvpn_plugin_close_v1(openvpn_plugin_handle_t handle)
{
    struct plugin_context *context = (struct plugin_context *)handle;
    free(context);
}