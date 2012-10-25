//////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2011-2012, OblakSoft LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// 
//
// Author: Maxim Mazeev <mazeev@hotmail.com>

//////////////////////////////////////////////////////////////////////////////
// Perf-only unit-test.
//////////////////////////////////////////////////////////////////////////////


#include "s3conn.h"
#include "sysutils.h"

#include <string.h>
#include <algorithm>
#include <fstream> 
#include <iostream>
#include <memory>
#include <sstream>

using namespace webstor;
using namespace webstor::internal;

#ifdef PERF

static const size_t KB = 1024;
static const size_t MB = KB * 1024;
static const UInt64 SEC = 1000;
static const UInt64 MINUTE = SEC * 60;

static S3Config s_config = {};
static const char *s_bucketName = NULL;

static const size_t s_iterationCount = 100;
static const size_t s_connectionCount = 64;
static const char s_dumpFile[] = "";

static const size_t s_objectSize = 256 * KB;
static const size_t s_objectSizeMax = 1 * MB;
static unsigned char s_writeData[ s_objectSizeMax ] = {};
static unsigned char s_readBufs[ s_connectionCount ][ s_objectSizeMax ] = {};

static std::auto_ptr< S3Connection > s_cons[ s_connectionCount ];
static AsyncMan s_asyncMans[ 4 ];

static const char s_key[] = "tmp/perf/test.dat";
static const size_t  s_keyCount = 64;

static double s_samples[ s_iterationCount ] = {};

static UInt64 s_cooldown = 10 * SEC;


#define DBG_RUN_UNIT_TEST( fn ) dbgRunUnitTest( fn, #fn )

static void
dbgRunUnitTest( void ( &fnTest )(), const char *name )
{
    std::cout << "Running " << name << "...";
    std::cout.flush();

    try
    {
        fnTest();
        std::cout << " done." << std::endl;
    }
    catch( ... )
    {
        std::cout << " failed." << std::endl;
        throw;
    }
}

static void
diffs( double *samples, size_t *count )
{
    dbgAssert( samples );
    dbgAssert( count && *count );

    for( size_t i = 1; i < *count; ++i )
    {
        samples[ i - 1 ] = samples[ i ] - samples[ i - 1 ];
    }

    samples[ *count - 1 ] = 0;
    --( *count );
}

static void
dumpSamples( const char* test, double *samples, size_t count )
{
    dbgAssert( test );
    dbgAssert( samples );
    dbgAssert( count > 0 );

    if( s_dumpFile && s_dumpFile[ 0 ] )
    {
        std::fstream dump( s_dumpFile, std::fstream::out | std::fstream::app );

        for( size_t i = 0; i < count; ++i )
        {
            dump << test << '\t' << i << '\t' << samples[ i ] << std::endl;
        }
    }
}

static double 
average( const double *samples, size_t count )
{
    dbgAssert( samples );
    dbgAssert( count > 0 );

    double a = 0;

    for( size_t i = 0; i < count; ++i )
    {
        a += samples[ i ];
    }

    return a / count;
}

static double 
median( double *samples, size_t count )
{
    dbgAssert( samples );
    dbgAssert( count > 0 );

    std::sort( samples, samples + count );

    size_t m = count / 2;
    return ( count & 1 ) ?  samples[ m ] : ( samples[ m - 1 ] + samples[ m ] ) / 2;
}


static void 
appendSample( double *samples, size_t size, size_t *current, double value )
{
    if( *current >= size )
    {
        // Shift elements.

        for( size_t i = 1; i < size; ++i )
        {
            samples[ i - 1 ] = samples[ i ];
        }

        samples[ size - 1 ] = value;
    }
    else
    {
        samples[ ( *current )++ ] = value;
    }
}

static void 
print( const char *test, double *samples, size_t count, bool calcDiff = true )
{
    dbgAssert( test );
    dbgAssert( samples );

    if( count <= 1 )
    {
         std::cout << test << "\t<empty>" << std::endl;
         return;
    }

    if( calcDiff )
    {
        diffs( samples, &count );
    }

    dumpSamples( test, samples, count );
    std::cout << test << "\t" << average( samples, count ) << '\t' << median( samples, count ) << std::endl;
}

static void
printError( S3Connection *conn = NULL )  // nofail
{
    // This function must be called from inside of a catch( ... ).

    if( conn != NULL )
    {
        std::cout << "Connection: " << std::hex << ( const void* )conn 
            << std::dec << " ";
    }

    try
    {
        throw;
    }
    catch( const std::exception &e )
    {
        std::cout << "Exception: " << e.what() << std::endl;
    }
    catch( ... )
    {
        std::cout << "Unknown Exception" << std::endl;
    }
}

static void
handleError()  // nofail
{
    printError();
}

static std::string
getKey( int i )
{
    std::stringstream tmp;
    tmp << s_key << '_' << i;
    return tmp.str();
}

static bool 
testGet( int iconn, int iasyncMan, int key, size_t objectSize )
{
    try
    {
        S3GetResponse response;
        s_cons[ iconn ]->get( s_bucketName, getKey( key ).c_str(), s_readBufs[ iconn ], s_objectSize,
            &response );
        return response.loadedContentLength != -1;
    }
    catch( ... )
    {
        printError( s_cons[ iconn ].get() );
    }
    return false;
}

static bool 
testPut( int iconn, int iasyncMan, int key, size_t objectSize )
{
    try
    {
        s_cons[ iconn ]->put( s_bucketName, getKey( key ).c_str(), s_writeData, s_objectSize );
        return true;
    }
    catch( ... )
    {
        printError( s_cons[ iconn ].get() );
    }
    return false;
}

static bool 
testPutDel( int iconn, int iasyncMan, int key, size_t objectSize )
{
    try
    {
        testPut( iconn, iasyncMan, key, objectSize );
        s_cons[ iconn ]->del( s_bucketName, getKey( key ).c_str() );
        return true;
    }
    catch( ... )
    {
        printError( s_cons[ iconn ].get() );
    }
    return false;
}

static bool 
testPendGet( int iconn, int iasyncMan, int key, size_t /* objectSize */ )
{
    try
    {
        s_cons[ iconn ]->pendGet( &s_asyncMans[ iasyncMan ], s_bucketName, 
            getKey( key ).c_str(), s_readBufs[ iconn ], s_objectSizeMax );
    }
    catch( ... )
    {
        printError( s_cons[ iconn ].get() );
    }
    return false;
}

static bool 
testCompleteGet( int iconn, int iasyncMan, int key, size_t objectSize )
{
    try
    {
        S3GetResponse response;
        s_cons[ iconn ]->completeGet( &response );
        return response.loadedContentLength == objectSize && !response.isTruncated;
    }
    catch( ... )
    {
        printError( s_cons[ iconn ].get() );
    }
    return false;
}

static bool 
testAsyncGet( int iconn, int iasyncMan, int key, size_t objectSize )
{
    testPendGet( iconn, iasyncMan, key, objectSize );
    testCompleteGet( iconn, iasyncMan, key, objectSize );
    return true;
}

static bool 
testPendPut( int iconn, int iasyncMan, int key, size_t objectSize )
{
    try
    {
        s_cons[ iconn ]->pendPut( &s_asyncMans[ iasyncMan ], s_bucketName, 
            getKey( key ).c_str(), s_writeData, objectSize );
        return true;
    }
    catch( ... )
    {
        printError( s_cons[ iconn ].get() );
    }
    return false;
}

static bool 
testCompletePut( int iconn, int iasyncMan, int key, size_t /* objectSize */ )
{
    try
    {
        s_cons[ iconn ]->completePut();
        return true;
    }
    catch( ... )
    {
        printError( s_cons[ iconn ].get() );
    }
    return false;
}

static bool 
testAsyncPut( int iconn, int iasyncMan, int key, size_t objectSize )
{
    testPendPut( iconn, iasyncMan, key, objectSize );
    testCompletePut( iconn, iasyncMan, key, objectSize );
    return true;
}

static bool 
testAsyncPutDel( int iconn, int iasyncMan, int key, size_t objectSize )
{
    testAsyncPut( iconn, iasyncMan, key, objectSize );
    s_cons[ iconn ]->pendDel( &s_asyncMans[ iasyncMan ], s_bucketName, getKey( key ).c_str() );
    s_cons[ iconn ]->completeDel();
    return true;
}

static const char *
encodeSpecialChars( const char *p, size_t size, std::string *pout )
{
    dbgAssert( p );
    dbgAssert( pout );
    
    // Search for the first special character.

    const char *cur = p;
    size_t curSize = size;

    for( ; curSize; ++cur, --curSize )
    {
        switch( *cur )
        {
            case '\\': 
            case '\a': 
            case '\b': 
            case '\f': 
            case '\n': 
            case '\r': 
            case '\t': 
            case '\v': 
            case '\0': 
                goto LNeedEncode;
        }
    }

    // We don't have any special characters in the original data.

    if( !*cur )
    {
        // and it's null-terminated, we can return the original string.

        return p;
    }

    // Copy to the buffer to null-terminate.

    pout->clear();
    pout->assign( p, size );
    return pout->c_str();

LNeedEncode:

    // Preallocate 10% more (or +10 for small strings) to reduce a chance of 
    // potential re-sizings.

    pout->clear();
    pout->reserve( size + size / 10 + 10 ); 
    const char *start = p;

    for( ; curSize; ++cur, --curSize )
    {
        const char *replacement = NULL;

        switch( *cur )
        {
            case '\\': replacement = "\\\\"; break;
            case '\a': replacement = "\\a"; break;
            case '\b': replacement = "\\b"; break;
            case '\f': replacement = "\\f"; break;
            case '\n': replacement = "\\n"; break;
            case '\r': replacement = "\\r"; break;
            case '\t': replacement = "\\t"; break;
            case '\v': replacement = "\\v"; break;
            case '\0': replacement = "\\0"; break;
        }

        if( replacement )
        {
            if( cur - start )
            {
                pout->append( start, cur );
            }

            dbgAssert( strlen( replacement ) == 2 );
            pout->append( replacement, 2 );

            start = cur + 1;
        }
    }

    if( cur - start )
    {
        pout->append( start, cur );
    }

    return pout->c_str();
}

static int      
httpTraceCallback( void *, TraceInfo type, unsigned char *data, size_t size, void *cookie )
{
    // Truncate the string.

    size = std::min( size, ( size_t ) 512 );

    // Encode special characters.

    std::string buf;
    const char *encoded = encodeSpecialChars( reinterpret_cast< const char * >( data ), size, &buf );

    LOG_TRACE( "%llu [%d] conn=0x%llx, %s\n", timeElapsed(), type, ( UInt64 )cookie, encoded ); 
    return 0;
}

typedef bool ( *TestFunc )( int iconn, int iasyncMan, int key, size_t objectSize );

struct Test
{
    const char     *name;
    TestFunc        test;
    TestFunc        testComplete;
};


void
perfTestS3Connection()
{
    // Check env. variables, if they are not set, skip the rest of the test.

    S3Config config = {};

    // Mandatory variables.

    if( !( config.accKey = getenv( "AWS_ACCESS_KEY" ) ) ||
        !( config.secKey = getenv( "AWS_SECRET_KEY" ) ) ||
        !( s_bucketName = getenv( "AWS_BUCKET_NAME" ) ) )
    {
        std::cout << "skip Amazon/Walrus test because no AWS_XXXX is set. ";
        return;
    }

    // Optional variables.

    config.host = getenv( "AWS_HOST" );

    if( config.host && *config.host && !strstr( config.host, "amazonaws.com" ) )
    {    
        config.isWalrus = true;
    }

    config.proxy = getenv( "AWS_PROXY" );

    // Print any background errors.

    setBackgroundErrHandler( &handleError );

    // Instantiate S3 connections.

    for( size_t i = 0; i < dimensionOf( s_cons ); ++i )
    {
        s_cons[ i ].reset( new S3Connection( config ) );

        // Enable HTTP tracing.

        // s_cons[ i ]->enableTracing( httpTraceCallback );
    }

    // Populate test data.
        
    std::cout << std::endl;
    std::cout << "populate test data." << std::endl;

    for( size_t i = 0; i < s_objectSize; ++i )
    {
        s_writeData[ i ] = ( unsigned char )( i % 256 );
    }

    Stopwatch stopwatch;

    // Warm-up, touch all connections.

    std::cout << "upload data and warm up." << std::endl;

    for( size_t i = 0; i < std::max( dimensionOf( s_cons ), s_keyCount ); ++i )
    {
        testAsyncPut( i % dimensionOf( s_cons ), i % dimensionOf( s_asyncMans ), i, s_objectSize );
        testGet( i % dimensionOf( s_cons ), 0 /* not used */, i, s_objectSize );
    }

    // Test Single operation.

    std::cout << std::endl << "test response with a single connection." << std::endl;
    std::cout << "name\tresponse(average in msecs)\tresponse(median in msecs)" << std::endl;

    static const Test tests[] = 
    { 
        { "get", testGet }, 
        { "async_get", testAsyncGet },
        { "put", testPut }, 
        { "async_put", testAsyncPut },
        { "put_del", testPutDel }, 
        { "async_put_del", testAsyncPutDel }
    };

    for( int t = 0; t < dimensionOf( tests ); ++t )
    {
        stopwatch.start();

        for( size_t i = 0; i < dimensionOf( s_samples ); ++i )
        {
            // Use 1 connection.

            dbgVerify( tests[ t ].test( 0, 0, i % s_keyCount, s_objectSize ) );
            s_samples[ i ] = stopwatch.elapsed();
        }

        print( tests[ t ].name, s_samples, dimensionOf( s_samples ) );

        taskSleep( s_cooldown );
    }

    // Test One AsyncMan vs. multiple AsyncMans.

    std::cout << std::endl << "test one AsyncMan vs. multiple AsyncMans." << std::endl;
    std::cout << "name\tresponse(average in msecs)\tresponse(median in msecs)" << std::endl;

    static const char *tests2[] = { "async_put_one_async_man", "async_put_multiple_async_mans" };

    stopwatch.start();

    for( int t = 0; t < dimensionOf( tests2 ); ++t )
    {
        for( size_t i = 0; i < dimensionOf( s_samples ); ++i )
        {
            size_t cons = std::min( dimensionOf( s_asyncMans ), dimensionOf( s_cons ) );

            for( size_t c = 0; c < cons; ++c )
            {
                testPendPut( c, ( t == 0 ? 0 : c ), i, s_objectSize );
            }
            for( size_t c = 0; c < cons; ++c )
            {
                testCompletePut( c, ( t == 0 ? 0 : c ), i, s_objectSize );
            }

            s_samples[ i ] = stopwatch.elapsed();
        }

        print( tests2[ t ], s_samples, dimensionOf( s_samples ) );

        taskSleep( s_cooldown );
    }

    // Test throughput.

    std::cout << std::endl << "test response and throughput with multiple connections." << std::endl;
    std::cout << "name\tobjectSize(bytes)\tconnections\ttotal(bytes)\tbytes per sec\ttps(ops per sec)"
        "\telapsed(msecs)\terrors\tkeyCount\tresponse(average in msecs)\tresponse(median in msecs)" << std::endl;

    const size_t objectSizes[] = { 4 * KB, 16 * KB, 64 * KB, 256 * KB, 1 * MB };

    Test tests3[] = 
    { 
        { "async_put", testPendPut, testCompletePut },
        { "async_get", testPendGet, testCompleteGet } // reads the objects created by put.
    };

    const UInt64 testDuration = 1 * MINUTE;

    // Allocate a temp array to wait for completion.

    std::vector< S3Connection *> cons;
    cons.resize( dimensionOf( s_cons ) );
    
    for( int i = 0; i < dimensionOf( s_cons ); ++i )
    {
        cons[ i ] = s_cons[ i ].get();
    }
    
    // Iterate through all object sizes.

    for( int o = 0; o < dimensionOf( objectSizes ); ++o )
    {
        size_t objectSize = objectSizes[ o ];
        size_t isample = 0;

        // Iterate through all connection counts.

        for( int c = 1; c <= dimensionOf( s_cons ); c *= 2 )
        {
            size_t putKeyCount = 0;

            // Iterate through all tests (put and get).

            for( int t = 0; t < dimensionOf( tests3 ); ++t )
            {
                TestFunc testPend = tests3[ t ].test;
                TestFunc testComplete = tests3[ t ].testComplete;

                // Prepare samples.

                memset( s_samples, 0, sizeof( s_samples ) );
                size_t isample = 0;

                UInt64 conSamples[ dimensionOf( s_cons ) ] = {};
                int conKeys[ dimensionOf( s_cons ) ] = {};

                // Start async for all connections.

                int key = 0;

                for( int k = 0; k < c; ++k, ++key )
                {
                   testPend( k, 0, key, objectSize );
                   conKeys[ k ] = key;
                }

                UInt64 total = 0;
                UInt64 elapsed = 0;
                UInt64 errors = 0;

                stopwatch.start();

                // Run the current test for the 'testDuration'.

                for(  ;( elapsed = stopwatch.elapsed() ) < testDuration; ++key )
                {
                    if( putKeyCount != 0  && key >= putKeyCount )
                    {
                        // This is the get test and we fetched all objects, stop.

                        break;
                    }

                    // Wait for any completed request.

                    //$ WARNING: this logic would need to be tweaked if
                    // the number of connections > 64 to wait on 64
                    // connections with a sliding window.

                    int k = S3Connection::waitAny( &cons[ 0 ], c, key % c /* startFrom */ );
                    dbgAssert( k >= 0 && k < c );

                    // Complete the current.

                    if( testComplete( k, 0, conKeys[ k ], objectSize ) )
                    {
                        total += objectSize;

                        elapsed = stopwatch.elapsed();
                        appendSample( s_samples, dimensionOf( s_samples ), &isample, elapsed - conSamples[ k ] );
                        conSamples[ k ] = elapsed;
                    }
                    else
                    {
                        errors++;
                    }

                    // Start a new.

                    testPend( k, 0, key, objectSize );
                    conKeys[ k ] = key;
                }

                if( putKeyCount == 0 )
                {
                    putKeyCount = key;
                }

                // Complete all.

                for( int k = 0; k < c; ++k )
                {
                    if( s_cons[ k ]->isAsyncPending() )
                    {
                        testComplete( k, 0, conKeys[ k ], objectSize );
                    }
                }

                dbgAssert( total <= objectSize * key ); // it can be less because we don't count objects 
                                                        // after testDuration elapsed.

                UInt64 bps = elapsed > 0 ? total * 1000ULL / elapsed : 0;  // bytes per second
                UInt64 tps = bps / objectSize;

                std::stringstream testName;
                testName << tests3[ t ].name << '\t' 
                    << objectSize  << '\t' 
                    << c << '\t' 
                    << total << '\t' 
                    << bps << '\t' 
                    << tps << '\t' 
                    << elapsed << '\t'
                    << errors << '\t'
                    << putKeyCount;
                print( testName.str().c_str(), s_samples, isample, false /* calcDiffs */ );

                taskSleep( s_cooldown );
            }
        }
    }
}

#else
#define DBG_RUN_UNIT_TEST( fn ) 
#endif  // PERF


int
main( int argc, char **argv )
{
#ifdef DEBUG
    if( argc > 1 && *argv[ 1 ] == 'd' )
        dbgBreak_;
#endif

    try
    {
        DBG_RUN_UNIT_TEST( perfTestS3Connection );
    }
    catch( const std::exception &e )
    {
        std::cout << std::endl << e.what() << std::endl;
    }
    catch( const char *s )
    {
        std::cout << std::endl << s << std::endl;
    }
    catch( ... )
    {
        std::cout << std::endl << "Unknown error" << std::endl;
    }
}
