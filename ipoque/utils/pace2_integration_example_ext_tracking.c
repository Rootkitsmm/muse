/********************************************************************************/
/**
 ** \file       pace2_integration_example_ext_tracking.c
 ** \brief      PACE 2 integration example with external tracking.
 ** \date       Jul 22, 2014
 ** \version    1.0
 ** \copyright  ipoque GmbH
 **
 ** This is a simple program to show the integration of the PACE 2 library.
 ** The program uses external tracking with the PACE 2 polling hash table.
 **/
/********************************************************************************/
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

/* Flow and subscriber hash tables */
static struct pace2_pht *flow_pht;
static struct pace2_pht *subscr_pht;

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

    fprintf( stderr, "\n" );
    fprintf( stderr, "Packet counter: %llu\n", packet_counter );
    fprintf( stderr, "\n" );

    if ( license_exceeded_packets > 0 ) {
        fprintf( stderr, "License exceeded packets: %llu.\n\n", license_exceeded_packets );
    }
} /* pace_print_results */

/* Configure and initialize PACE 2 module and hash tables. */
static void pace_configure_and_initialize(  const char * const license_file )
{
    /* Initialize configuration with default values */
    pace2_init_default_config( &config );
    pace2_set_license_config( &config, license_file );

    /* Set necessary memory wrapper functions */
    config.general.pace2_alloc = malloc_wrapper;
    config.general.pace2_free = free_wrapper;
    config.general.pace2_realloc = realloc_wrapper;

    /* Set external tracking for flows and subscribers */
    config.tracking.flow.generic.type = EXTERNAL;
    config.tracking.subscriber.generic.type = EXTERNAL;

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
    } /* Licensing */

    { /* Flow hash table */
        struct PACE2_pht_config pht_conf;

        /* Initialize config structure */
        pace2_pht_init_default_config( &pht_conf );

        /* Use 4MB memory for the hash table */
        pht_conf.memory_size = 4 * 1024 * 1024;

        /* Set the size of the element key. For flows the 5 tuple
           (src ip [16], dst ip [16], src port [2], dst port [2], l4 protocol [1]) is used.
           This makes a total of 37 bytes for the key, however they key size must be
           a multiple of 4 which is why 40 is used here. The unused bytes will be zeroed out. */
        pht_conf.unique_key_size = 40;

        /* The size of memory required for every element. */
        pht_conf.user_buffer_size = pace2_get_flow_memory_size( pace2, 0 );

        /* 10 Minutes element timeout. */
        pht_conf.timeout = 10 * 60 * config.general.clock_ticks_per_second;

        /* Set memory allocation/deallocation functions for the hash table */
        pht_conf.ipq_malloc = malloc_wrapper;
        pht_conf.ipq_free = free_wrapper;

        /* Initialize the hash table */
        flow_pht = pace2_pht_create( &pht_conf, 0 );

        if ( flow_pht == NULL ) {
            panic( "Initialization of flow hash table failed\n" );
        }
    } /* Flow hash table */

    { /* Subscriber hash table */
        struct PACE2_pht_config pht_conf;

        /* Initialize config structure */
        pace2_pht_init_default_config( &pht_conf );

        /* Use 4MB memory for the hash table */
        pht_conf.memory_size = 4 * 1024 * 1024;

        /* Set the size of the element key. For subscribers this is a single ip address.
           For IPv6 16 bytes are needed per address. The remaining bytes will be zeroed for IPv4. */
        pht_conf.unique_key_size = 16;

        /* The size of memory required for every element. */
        pht_conf.user_buffer_size = pace2_get_subscriber_memory_size( pace2, 0 );

        /* 10 Minutes element timeout. */
        pht_conf.timeout = 10 * 60 * config.general.clock_ticks_per_second;

        /* Set memory allocation/deallaction functions for the hash table */
        pht_conf.ipq_malloc = malloc_wrapper;
        pht_conf.ipq_free = free_wrapper;

        /* Initialize the hash table */
        subscr_pht = pace2_pht_create( &pht_conf, 0 );

        if ( subscr_pht == NULL ) {
            panic( "Initialization of subscriber hash table failed\n" );
        }
    } /* Subscriber hash table */
} /* pace_configure_and_initialize */

static void *pace2_get_subscriber( PACE2_packet_descriptor * const pd, const u8 use_dst )
{
    PACE2_subscriber_key key;
    void *subscr;
    u8 new_subscr = 0;
    pace2_build_subscriber_key( pd, &key, 0, use_dst );

    subscr = pace2_pht_insert( subscr_pht, (u8 *) &key.buffer[0], &new_subscr );

    if ( subscr != NULL && new_subscr != 0 ) {
        memset( subscr, 0, pace2_pht_get_user_buffer_size( subscr_pht ) );
    }

    return subscr;
} /* pace2_get_subscriber */

static void *pace2_get_flow( PACE2_packet_descriptor * const pd,
                             const uint64_t time)
{
    ipoque_unique_flow_ipv4_and_6_struct_t key;

    if ( pace2_build_flow_key( pd, &key, NULL, 0 ) == PACE2_SUCCESS ) {
        u8 new_flow;
        void * const flow = pace2_pht_insert( flow_pht, (u8 *) &key, &new_flow );

        if ( flow != NULL && new_flow != 0 ) {
            /* Initialize flow memory */
            memset( flow, 0, pace2_pht_get_user_buffer_size( flow_pht ) );
        }

        return flow;
    }

    return NULL;
} /* pace2_get_flow */

static void stage3_to_5( void )
{
    const PACE2_event *event;
    PACE2_bitmask pace2_event_mask;
    PACE2_packet_descriptor *out_pd;

    /* Process stage 3 and 4 as long as packets are available from stage 2 */
    while ( ( out_pd = pace2_s2_get_next_packet( pace2, 0 ) ) ) {
        /* Account every processed packet */
        packet_counter++;
        byte_counter += out_pd->framing->stack[0].frame_length;

        switch ( pace2_s3_process_packet( pace2, 0, out_pd, &pace2_event_mask ) ) {
            case PACE2_S3_REQUIRE_SUBSCRIBER:
                /* Do subscriber lookup for slowpath. */
                out_pd->src_stage3 = pace2_get_subscriber( out_pd, 0 );
                out_pd->dst_stage3 = pace2_get_subscriber( out_pd, 1 );

                /* Process stage 3: packet classification */
                if ( pace2_s3_process_packet( pace2, 0, out_pd, &pace2_event_mask ) != PACE2_S3_SUCCESS ) {
                    continue;
                }

                break;
            case PACE2_S3_SUCCESS:
                break;

            default:
                continue;
        } /* Stage 3 */

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
            }
        }
    }

    /* Process stage 5: timeout handling */
    if ( pace2_s5_handle_timeout( pace2, 0, &pace2_event_mask ) != 0 ) {
        return;
    }
}

static void stage1_and_2( const uint64_t time, const struct iphdr *iph, uint16_t ipsize )
{
    PACE2_packet_descriptor pd;

    /* Stage 1: Prepare packet descriptor and run ip defragmentation */
    if (pace2_s1_process_packet( pace2, 0, time, iph, ipsize, PACE2_S1_L3, &pd, NULL, 0 ) != PACE2_S1_SUCCESS) {
        return;
    }

    /* Set the current timestamp for the hash tables */
    pace2_pht_set_timestamp( flow_pht, time );
    pace2_pht_set_timestamp( subscr_pht, time );

    /* Set unique packet id. */
    pd.packet_id = ++next_packet_id;

    /* Do flow tracking and set the flow pointer. */
    pd.flow_data = pace2_get_flow(&pd, time);

    if (pd.flow_data == NULL) {
        return;
    }

    /* Stage 2: Packet reordering */
    if ( pace2_s2_process_packet( pace2, 0, &pd ) != PACE2_S2_SUCCESS ) {
        return;
    }

    stage3_to_5();

    /* Reserve flow elements for the next insert */
    pace2_pht_reserve_elements( flow_pht, 1 );
    {
        void *p;

        while ( ( p = pace2_pht_get_next_element_to_remove( flow_pht, NULL, NULL ) ) ) {
            pace2_release_flow( pace2, 0, p );

            stage3_to_5();
        }
    }
} /* stage1_and_2 */

static void pace_cleanup_and_exit( void )
{
    /* Clear the flow hash table and handle each removed flow. */
    pace2_pht_clear( flow_pht );
    {
        void *p;

        while ( ( p = pace2_pht_get_next_element_to_remove( flow_pht, NULL, NULL ) ) ) {
            pace2_release_flow( pace2, 0, p );

            stage3_to_5();
        }
    }

    /* Flush any remaining packets from the buffers */
    pace2_flush_engine( pace2, 0 );

    /* Process packets which are ejected after flushing */
    stage3_to_5();

    /* Output detection results */
    pace_print_results();

    /* Destroy the hash tables */
    pace2_pht_destroy( flow_pht );
    pace2_pht_destroy( subscr_pht );

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
