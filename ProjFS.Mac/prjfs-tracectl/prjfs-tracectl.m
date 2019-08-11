#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include "../PrjFSKext/public/PrjFSCommon.h"
#include <unistd.h>
#include <getopt.h>
#import <Foundation/Foundation.h>

static CFTypeRef GenerateEventTracingSettings(int argc, char* argv[])
{
    CFStringRef pathFilterString = NULL;
    int tracingEnabled = 0, tracingDisabled = 0;
    bool useVnodeActionFilter = false;
    uint32_t vnodeActionFilter = UINT32_MAX;
    int traceAllVnodeEvents = 0, traceDeniedVnodeEvents = 0, traceProviderMessagingEvents = 0;
    int traceAllFileopEvents = 0;

    int ch;

    struct option longopts[] = {
         { "enable",               no_argument,            &tracingEnabled,  1 },
         { "disable",              no_argument,            &tracingDisabled, 1 },
         { "vnode-events-denied",  no_argument,            &traceDeniedVnodeEvents, 1 },
         { "vnode-message-events", no_argument,            &traceProviderMessagingEvents, 1 },
         { "vnode-all-events",     no_argument,            &traceAllVnodeEvents, 1 },
         { "fileop-all-events",    no_argument,            &traceAllFileopEvents, 1 },
         { "path-filter",          required_argument,      NULL,             'p' },
         { "vnode-action-filter",  required_argument,      NULL,             'a' },
         { NULL,                   0,                      NULL,             0 }
    };

    while ((ch = getopt_long(argc, argv, "bf:", longopts, NULL)) != -1)
    {
        switch (ch)
        {
        case 'a':
            {
                char* end = NULL;
                unsigned long filter = strtoul(optarg, &end, 16 /* base */);
                if (end != optarg && end != NULL && filter <= UINT32_MAX)
                {
                    useVnodeActionFilter = true;
                    vnodeActionFilter = (uint32_t)filter;
                }
                else
                {
                    fprintf(stderr, "--vnode-action-filter: Bad filter mask value, must be in range 0-ffffffff");
                    goto CleanupAndFail;
                }
            }
            
            break;
        case 'p':
            if (pathFilterString != NULL)
            {
                fprintf(stderr, "--path-filter: Currently only one path filter is supported\n");
                goto CleanupAndFail;
            }
            
            pathFilterString = CFStringCreateWithCString(kCFAllocatorDefault, optarg, kCFStringEncodingUTF8);
            
            break;
        case 0:
            printf("Processing argument %s\n", argv[optind]);
            break;
        default:
            fprintf(stderr, "TODO: %u\n", ch);
            goto CleanupAndFail;
        }
    }
    
    if ((tracingEnabled ^ tracingDisabled) == 0)
    {
        fprintf(stderr, "Must use exactly one of --enable or --disable");
        goto CleanupAndFail;
    }
    else if (pathFilterString == NULL)
    {
        fprintf(stderr, "--path-filter is required");
        goto CleanupAndFail;
    }

    if (tracingDisabled)
    {
        if (pathFilterString != NULL)
        {
            CFRelease(pathFilterString);
        }
        return kCFBooleanFalse;
    }
    else
    {
        NSMutableDictionary* settings = [[NSMutableDictionary alloc] initWithCapacity:5];
        if (traceAllVnodeEvents)
        {
            [settings setObject:[NSNumber numberWithBool:YES] forKey:@"vnode-events-all"];
        }
        else
        {
            [settings setObject:[NSNumber numberWithBool:(traceDeniedVnodeEvents != 0)] forKey:@"vnode-events-denied"];
            [settings setObject:[NSNumber numberWithBool:(traceProviderMessagingEvents != 0)] forKey:@"vnode-message-events"];
        }
        
        if (pathFilterString != NULL)
        {
            [settings setObject:(__bridge_transfer NSString*)pathFilterString forKey:@"path-filter"];
        }
        
        if (useVnodeActionFilter)
        {
            [settings setObject:@(vnodeActionFilter) forKey:@"vnode-action-filter-mask"];
        }
        
        [settings setObject:[NSNumber numberWithBool:traceAllFileopEvents] forKey:@"fileop-all-events"];
        
        NSLog(@"Settings dictionary: %@", settings);
        
        return (__bridge_retained CFDictionaryRef)settings;
    }
    
CleanupAndFail:
    if (pathFilterString != NULL)
    {
        CFRelease(pathFilterString);
    }
    
    return NULL;
}

int main(int argc, char* argv[])
{
    CFTypeRef traceSettings = GenerateEventTracingSettings(argc, argv);
    if (traceSettings == NULL)
    {
        fprintf(stderr, "Failed to generate tracing settings\n");
        return 1;
    }

    CFDictionaryRef matchDict = IOServiceMatching(PrjFSServiceClass);
    io_service_t prjfsService = IOServiceGetMatchingService(kIOMasterPortDefault, matchDict); // matchDict consumed
    
    if (prjfsService == IO_OBJECT_NULL)
    {
        CFRelease(traceSettings);
        fprintf(stderr, "PrjFS Service object not found.\n");
        return 1;
    }
    
    IORegistryEntrySetCFProperty(prjfsService, CFSTR(PrjFSEventTracingKey), traceSettings);
    CFRelease(traceSettings);
    
    IOObjectRelease(prjfsService);
    return 0;
}
