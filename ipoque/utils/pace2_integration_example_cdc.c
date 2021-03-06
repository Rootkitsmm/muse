/********************************************************************************/
/**
 ** \file       pace2_integration_example_cdc.c
 ** \brief      PACE 2 integration example with internal tracking showing CDC capabilities.
 ** \date       Mar 19, 2015
 ** \version    1.0
 ** \copyright  ipoque GmbH
 **
 ** This is a simple program to show the integration of the PACE 2 library.
 ** The program uses the internal tracking feature of the library.
 ** Additionally it shows how to do matching with custom defined classification feature.
 **/
/********************************************************************************/

#ifdef __linux__
#include <netinet/tcp.h>
#endif

#ifdef WIN32
#include <WinSock2.h>
#include <inttypes.h>
#endif

#include <pace2.h>
#include "read_pcap.h"
#include "event_handler.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* PACE 2 module pointer */
static PACE2_module *pace2 = NULL;

/* PACE 2 configuration structure */
static struct PACE2_global_config config;

#define PACE2_INT_EXAMPLE_CDC_COUNT 3

/* Result counters */
static u64 packet_counter = 0;
static u64 byte_counter = 0;
static u64 protocol_counter[PACE2_PROTOCOL_COUNT];
static u64 protocol_counter_bytes[PACE2_PROTOCOL_COUNT];
static u64 protocol_stack_length_counter[PACE2_PROTOCOL_STACK_MAX_DEPTH];
static u64 protocol_stack_length_counter_bytes[PACE2_PROTOCOL_STACK_MAX_DEPTH];
static u64 application_counter[PACE2_APPLICATIONS_COUNT];
static u64 application_counter_bytes[PACE2_APPLICATIONS_COUNT];
static u64 attribute_counter[PACE2_APPLICATION_ATTRIBUTES_COUNT];
static u64 attribute_counter_bytes[PACE2_APPLICATION_ATTRIBUTES_COUNT];
static u64 http_response_payload_bytes = 0;
static u64 cdc_packets[PACE2_INT_EXAMPLE_CDC_COUNT];
static u64 cdc_bytes[PACE2_INT_EXAMPLE_CDC_COUNT];


static u64 next_packet_id = 0;

static u64 license_exceeded_packets = 0;

/* Protocol, application and attribute name strings */
static const char *prot_long_str[] = { PACE2_PROTOCOLS_LONG_STRS };
static const char *app_str[] = { PACE2_APPLICATIONS_SHORT_STRS };

/* Memory allocation wrappers */
static void *malloc_wrapper( u64 size,
                             int thread_ID,
                             void *user_ptr,
                             int scope )
{
    return malloc( size );
} /* malloc_wrapper */

static void free_wrapper( void *ptr,
                          int thread_ID,
                          void *user_ptr,
                          int scope )
{
    free( ptr );
} /* free_wrapper */

static void *realloc_wrapper( void *ptr,
                              u64 size,
                              int thread_ID,
                              void *user_ptr,
                              int scope )
{
    return realloc( ptr, size );
}


/* panic is used for abnormal errors (allocation errors, file not found,...) */
static void panic( const char *msg )
{
    printf( "%s", msg );
    exit( 1 );
} /* panic */

/* Print classification results to stderr */
static void pace_print_results( void )
{
    u32 c;
    fprintf( stderr, "  %-20s %-15s %s\n\n", "Protocol", "Packets", "Bytes" );
    for ( c = 0; c < PACE2_PROTOCOL_COUNT; c++ ) {
        if ( protocol_counter_bytes[c] != 0 ) {
            fprintf( stderr, "  %-20s %-15llu %llu\n",
                     prot_long_str[c], protocol_counter[c], protocol_counter_bytes[c] );
        }
    }
    fprintf( stderr, "\n\n" );
    fprintf( stderr, "  %-20s %-15s %s\n\n", "Stack length", "Packets", "Bytes" );
    for ( c = 0; c < PACE2_PROTOCOL_STACK_MAX_DEPTH; c++ ) {
        fprintf( stderr, "  %-20u %-15llu %llu\n",
                 c, protocol_stack_length_counter[c], protocol_stack_length_counter_bytes[c] );
    }
    fprintf( stderr, "\n\n" );
    fprintf( stderr, "  %-20s %-15s %s\n\n", "Application", "Packets", "Bytes" );
    for ( c = 0; c < PACE2_APPLICATIONS_COUNT; c++ ) {
        if ( application_counter_bytes[c] != 0 ) {
            fprintf( stderr, "  %-20s %-15llu %llu\n",
                     app_str[c], application_counter[c], application_counter_bytes[c] );
        }
    }
    fprintf( stderr, "\n\n" );
    fprintf( stderr, "  %-20s %-15s %s\n\n", "Attribute", "Packets", "Bytes" );
    for ( c = 0; c < PACE2_APPLICATION_ATTRIBUTES_COUNT; c++ ) {
        if ( attribute_counter[c] != 0 ) {
            fprintf( stderr, "  %-20s %-15llu %llu\n",
                     pace2_get_application_attribute_str(c), attribute_counter[c], attribute_counter_bytes[c] );
        }
    }
    fprintf( stderr, "\n\n" );
    fprintf( stderr, "  %-20s %-15s %s\n\n", "Custom Class", "Packets", "Bytes" );
    for ( c = 0; c < PACE2_INT_EXAMPLE_CDC_COUNT; c++ ) {
        if ( cdc_bytes[c] != 0 ) {
            fprintf( stderr, "  CDC%-18u %-15llu %llu\n",
                     c, cdc_packets[c], cdc_bytes[c] );
        }
    }

    fprintf( stderr, "\n" );
    fprintf( stderr, "HTTP response payload bytes: %llu\n", http_response_payload_bytes );
    fprintf( stderr, "\n" );

    fprintf( stderr, "\n" );
    fprintf( stderr, "Packet counter: %llu\n", packet_counter );
    fprintf( stderr, "\n" );

    if ( license_exceeded_packets > 0 ) {
        fprintf( stderr, "License exceeded packets: %llu.\n\n", license_exceeded_packets );
    }

} /* pace_print_results */

PACE2_cdc_return_state port_match_callback(PACE2_module * const pace2_module,
                                           PACE2_packet_descriptor *packet_descriptor,
                                           int thread_ID,
                                           void *userptr, void *flow_area,
                                           void *src_area, void *dst_area)
{
    int i;
    for (i = 0; i < packet_descriptor->framing->stack_size - 1; i++) {
        if (packet_descriptor->framing->stack[i].type == TCP ) {
            /* check for port 443 on either source or destination */
            if (packet_descriptor->framing->stack[i].frame_data.tcp->source == htons(443) ||
                packet_descriptor->framing->stack[i].frame_data.tcp->dest == htons(443)) {
                /* found, so match this CDC  */
                return PACE2_CDC_MATCH;
            }
            break;
        }
    }

    return PACE2_CDC_EXCLUDE;
}

PACE2_cdc_return_state class_result_match_callback(PACE2_module * const pace2_module,
                                                   PACE2_packet_descriptor *packet_descriptor,
                                                   int thread_ID,
                                                   void *userptr, void *flow_area,
                                                   void *src_area, void *dst_area)
{
    const PACE2_classification_result_event *res = pace2_cdc_get_classification(pace2_module, thread_ID);

    if (res != NULL && res->protocol.stack.length > PACE2_RESULT_PROTOCOL_LAYER_7 &&
        res->protocol.stack.entry[PACE2_RESULT_PROTOCOL_LAYER_7] == PACE2_PROTOCOL_DNS) {
        return PACE2_CDC_MATCH;
    }

    return PACE2_CDC_EXCLUDE;
}

PACE2_cdc_return_state poison_ivy_callback(PACE2_module * const pace2_module,
                                           PACE2_packet_descriptor *pd,
                                           int thread_ID,
                                           void *userptr, void *flow_area,
                                           void *src_area, void *dst_area)
{
    /* We need atleast TCP + L7 */
    if ( pd->framing->stack_size < 2 ) return PACE2_CDC_EXCLUDE;

    if ( pd->framing->stack[ pd->framing->stack_size - 2 ].type != TCP ) return PACE2_CDC_EXCLUDE;
    if ( pd->framing->stack[ pd->framing->stack_size - 1 ].type != L7 ) return PACE2_CDC_EXCLUDE;

    {
        const u8 * const payload = pd->framing->stack[ pd->framing->stack_size - 1 ].frame_data.l7_data;
        const u16 payload_len = pd->framing->stack[ pd->framing->stack_size - 1 ].frame_length;
        const struct PACE2_cdc_generic_info * const info = pace2_cdc_get_generic_info(pace2_module, thread_ID);
        const u16 flow_packet_counter = info->flow_packet_counter[0] + info->flow_packet_counter[1];
        u8 * const poison_ivy_stage = (u8 *)flow_area;

        /* skip packets without payload (syn, ack etc) */
        if (payload_len == 0) return PACE2_CDC_NEED_NEXT_PACKET;

        /* flow begins with two handshake packets each with 256 byte */
        if (flow_packet_counter < 3 && payload_len == 256) {
            return PACE2_CDC_NEED_NEXT_PACKET;
        }

        /* third packet contains a static header in the first 4 byte */
        if (flow_packet_counter == 3 &&
            payload_len > 1000 &&
            *(u32*)(payload) == htonl(0xd0150000) &&
            *poison_ivy_stage == 0) {

            *poison_ivy_stage = 1;

            return PACE2_CDC_NEED_NEXT_PACKET;
        }

        /* wait for fixed size packets which have to follow after ~10-20 packets */
        if (flow_packet_counter > 3 && flow_packet_counter < 25 &&
            *poison_ivy_stage > 0) {

            /* this packet must be from client and client must not have send more
               than 2 packets (including this one) until now */
            if (*poison_ivy_stage == 1 &&
                (payload_len == 224 || payload_len == 240) &&
                flow_packet_counter >= 10 &&
                info->packet_direction == info->initial_packet_direction &&
                info->flow_packet_counter[info->packet_direction] == 2) {

                *poison_ivy_stage = 2;

                return PACE2_CDC_NEED_NEXT_PACKET;
            }

            /* this packet comes from the server side, the client still must not have
               send more than two packets */
            if (*poison_ivy_stage == 2 &&
                payload_len == 48 &&
                info->packet_direction != info->initial_packet_direction &&
                info->flow_packet_counter[info->initial_packet_direction] == 2) {

                return PACE2_CDC_MATCH;
            }

            return PACE2_CDC_NEED_NEXT_PACKET;
        }
    }

    return PACE2_CDC_EXCLUDE;
}

/* Configure and initialize PACE 2 module */
static void pace_configure_and_initialize( const char * const license_file )
{
    struct PACE2_config_s3_cdc cust_class[PACE2_INT_EXAMPLE_CDC_COUNT];
    memset(cust_class, 0, sizeof(struct PACE2_config_s3_cdc) * PACE2_INT_EXAMPLE_CDC_COUNT);

    cust_class[0].callback = &port_match_callback;
    cust_class[0].callback_userptr = NULL;
    cust_class[0].flow_area_size = 0;
    cust_class[0].subscriber_area_size = 0;

    cust_class[1].callback = &class_result_match_callback;
    cust_class[1].callback_userptr = NULL;
    cust_class[1].flow_area_size = 0;
    cust_class[1].subscriber_area_size = 0;

    cust_class[2].callback = &poison_ivy_callback;
    cust_class[2].callback_userptr = NULL;
    cust_class[2].flow_area_size = 1;
    cust_class[2].subscriber_area_size = 0;

    /* Initialize configuration with default values */
    pace2_init_default_config( &config );
    pace2_set_license_config( &config, license_file );

    /* Set necessary memory wrapper functions */
    config.general.pace2_alloc = malloc_wrapper;
    config.general.pace2_free = free_wrapper;
    config.general.pace2_realloc = realloc_wrapper;

    config.s3_classification.cdc_number = PACE2_INT_EXAMPLE_CDC_COUNT;
    config.s3_classification.cdc_conf_array = &cust_class[0];

    /* set size of hash table for flow and subscriber tracking */
    // config.tracking.flow.generic.max_size.memory_size = 400 * 1024 * 1024;
    // config.tracking.subscriber.generic.max_size.memory_size = 400 * 1024 * 1024;

    /* The following features improves detection rate */

    /* Stage 1: enable IP defragmentation */
    // config.s1_preparing.defrag.enabled = 1;
    // config.s1_preparing.max_framing_depth = 10;
    // config.s1_preparing.max_decaps_level = 10;

    /* Stage 2: enable PARO and set necessary values */
    // config.s2_reordering.enabled = 1;
    // config.s2_reordering.packet_buffer_size = 16 * 1024 * 1024;
    // config.s2_reordering.packet_timeout = 5 * config.general.clock_ticks_per_second;

    /* Stage 3: enable specific classification components */
    // config.s3_classification.asym_detection_enabled = 1
    // config.s3_classification.sit.enabled = 1;
    // config.s3_classification.sit.key_reduce_factor = 1;
    // config.s3_classification.sit.memory = 1 * 1024 * 1024;

    // uncomment the following line to only activate classification of protocol HTTP
    // PACE2_PROTOCOLS_BITMASK_RESET(config.s3_classification.active_classifications.bitmask);
    // config.s3_classification.active_classifications.protocols.http = IPQ_TRUE;

    /* Stage 4: enable decoding */
    //config.s4_decoding.enabled = 1;
    // uncomment the following line to only activate decoding of protocol HTTP
    // PACE2_PROTOCOLS_BITMASK_RESET(config.s4_decoding.active_advanced_decoders.bitmask);
    // config.s4_decoding.active_advanced_decoders.protocols.http = IPQ_TRUE;

    /* Initialize PACE 2 detection module */
    pace2 = pace2_init_module( &config );

    if ( pace2 == NULL ) {
        panic( "Initialization of PACE module failed\n" );
    }

    /* Licensing */
    if ( license_file != NULL ) {

        PACE2_class_return_state retval;
        PACE2_classification_status_event lic_event;

        memset(&lic_event, 0, sizeof(PACE2_classification_status_event));
        retval = pace2_class_get_license(pace2, 0, &lic_event);
        if (retval == PACE2_CLASS_SUCCESS) {
            pace2_debug_event(stdout, (PACE2_event const * const) &lic_event);
        }
    }
} /* pace_configure_and_initialize */

/* Print out all PACE 2 events currently in the event queue */
static void process_events(void)
{
    PACE2_event *event;

    while ( ( event = pace2_get_next_event( pace2, 0 ) ) ) {
        /* some additional processing is necessary */
        if ( event->header.type == PACE2_CLASS_HTTP_EVENT ) {
            const PACE2_class_HTTP_event * const http_event = &event->http_class_meta_data;
            if (http_event->meta_data_type == PACE2_CLASS_HTTP_RESPONSE_DATA_TRANSFER) {
                const struct ipd_class_http_transfer_payload_struct * const http_payload = &http_event->event_data.response_data_transfer;
                http_response_payload_bytes += http_payload->data.content.length;
            }
        }
        pace2_debug_advanced_event(stdout, event);
    }
} /* process_events */

static void stage3_to_5( void )
{
    const PACE2_event *event;
    PACE2_bitmask pace2_event_mask;
    PACE2_packet_descriptor *out_pd;

    /* Process stage 3 and 4 as long as packets are available from stage 2 */
    while ( (out_pd = pace2_s2_get_next_packet(pace2, 0)) ) {

        /* Account every processed packet */
        packet_counter++;
        byte_counter += out_pd->framing->stack[0].frame_length;

        /* Process stage 3: packet classification */
        if ( pace2_s3_process_packet( pace2, 0, out_pd, &pace2_event_mask ) != PACE2_S3_SUCCESS ) {
            continue;
        } /* Stage 3 processing */

        /* Get all thrown events of stage 3 */
        while ( ( event = pace2_get_next_event(pace2, 0) ) ) {
            /* some additional processing is necessary */
            if ( event->header.type == PACE2_CLASSIFICATION_RESULT ) {
                PACE2_classification_result_event const * const classification = &event->classification_result_data;
                u8 attribute_iterator;

                protocol_counter[classification->protocol.stack.entry[classification->protocol.stack.length-1]]++;
                protocol_counter_bytes[classification->protocol.stack.entry[classification->protocol.stack.length-1]] += out_pd->framing->stack[0].frame_length;
                protocol_stack_length_counter[classification->protocol.stack.length - 1]++;
                protocol_stack_length_counter_bytes[classification->protocol.stack.length - 1] += out_pd->framing->stack[0].frame_length;

                application_counter[classification->application.type]++;
                application_counter_bytes[classification->application.type] += out_pd->framing->stack[0].frame_length;

                for ( attribute_iterator = 0; attribute_iterator < classification->application.attributes.length; attribute_iterator++) {
                    attribute_counter[classification->application.attributes.list[attribute_iterator]]++;
                    attribute_counter_bytes[classification->application.attributes.list[attribute_iterator]] += out_pd->framing->stack[0].frame_length;
                }
            } else if ( event->header.type == PACE2_LICENSE_EXCEEDED_EVENT ) {
                license_exceeded_packets++;
            } else if ( event->header.type == PACE2_CDC_RESULT ) {
                PACE2_cdc_result_event const * const cdc = &event->cdc_data;

                cdc_packets[cdc->detected_cdc_id]++;
                cdc_bytes[cdc->detected_cdc_id] += out_pd->framing->stack[0].frame_length;

            }
        } /* Stage 3 event processing */

        /* Process stage 4: protocol decoding */
        if ( pace2_s4_process_packet( pace2, 0, out_pd, NULL, &pace2_event_mask ) != PACE2_S4_SUCCESS ) {
            continue;
        }

        /* Print out decoder events */
        process_events();

    } /* Stage 2 packets */

    /* Process stage 5: timeout handling */
    if ( pace2_s5_handle_timeout( pace2, 0, &pace2_event_mask ) != 0 ) {
        return;
    }

    /* Print out decoder events generated while cleaning up flows that timed out */
    process_events();

} /* stage3_to_5 */

static void stage1_and_2( const uint64_t time, const struct iphdr *iph, uint16_t ipsize )
{
    PACE2_packet_descriptor pd;

    /* Stage 1: Prepare packet descriptor and run ip defragmentation */
    if (pace2_s1_process_packet( pace2, 0, time, iph, ipsize, PACE2_S1_L3, &pd, NULL, 0 ) != PACE2_S1_SUCCESS) {
        return;
    }

    /* Set unique packet id. The flow_id is set by the internal flow tracking */
    pd.packet_id = ++next_packet_id;

    /* Stage 2: Packet reordering */
    if ( pace2_s2_process_packet( pace2, 0, &pd ) != PACE2_S2_SUCCESS ) {
        return;
    }

    stage3_to_5();
} /* stage1_and_2 */

static void pace_cleanup_and_exit( void )
{
    /* Flush any remaining packets from the buffers */
    pace2_flush_engine( pace2, 0 );

    /* Process packets which are ejected after flushing */
    stage3_to_5();

    /* Output detection results */
    pace_print_results();

    /* Destroy PACE 2 module and free memory */
    pace2_exit_module( pace2 );
} /* pace_cleanup_and_exit */

int main( int argc, char **argv )
{
    const char * license_file = NULL;

    /* Arg check */
    if ( argc < 2 ) {
        panic( "PCAP file not given, please give the pcap file as parameter\n" );
    } else if ( argc > 2 ) {
        license_file = argv[2];
    }

    /* Initialize PACE 2 */
    pace_configure_and_initialize( license_file );

    /* Read the pcap file and pass packets to stage1_and_2 */
    if ( read_pcap_loop( argv[1], config.general.clock_ticks_per_second, &stage1_and_2 ) != 0 ) {
        panic( "could not open pcap interface / file\n" );
    }

    pace_cleanup_and_exit();

    return 0;
} /* main */
