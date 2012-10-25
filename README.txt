  Copyright (c) 2011-2012, OblakSoft LLC.

  ----------------------------
  webstor library
  ----------------------------

  C++ library to access Amazon S3.  Provides several rare features such as 
  Amazon S3 multi-part upload, async support, HTTP proxy, HTTP tracing, 
  supports Eucalyptus Walrus. It is tuned to utilize HTTP stack efficiently, 
  and offers robust error handling. The library contains built-in SSL CA
  certificates required to establish secure SSL connection to Amazon S3. 

  
  Dependencies
  -------------
  The library was tested on Linux (2.6.27 and above) and Windows (Vista and
  above) both x86, x64.  It has the following dependencies: 
    
    - curl (http://curl.haxx.se/)
    - libxml2 (http://xmlsoft.org/)
    - openssl (http://www.openssl.org/)

  
  Licensing
  ---------

  Please see the file named LICENSE.txt.


  Build instructions (Linux)
  --------------------------

  1. Install dependencies. 
  
  For Red Hat distributions the dependent libraries can be installed using
  the following command:
        sudo yum install curl-devel openssl-devel libxml2-devel

  Make sure that build tools are installed too, if not, type:
        sudo yum install gcc-c++ make
  
  For Debian/Ubuntu distributions the dependent libraries can be installed
  using the following command:
      sudo apt-get install libcurl4-openssl-dev libxml2-dev 

  and build tools:
      sudo apt-get install g++ make

  If your distribution doesn't use apt / yum, please refer to your
  distribution's documentation.  Alternatively, you can download the
  latest sources for the dependencies from the corresponding websites
  (curl, xmlsoft and openssl) and compile them from source.
  
  2. Build webstor lib.
 
  The 'make' command can be used to build webstor.  If the dependent
  libraries are installed into non-standard locations, you may need to
  tweak makefile (see makefile comments for more information). 

  If build succeeds, in the build folder you will find s3test executable.
  s3test provides a command line interface to webstor API.  s3test command
  line parameters are described below.


  Package content
  ---------------
  webstor source files:
  
  s3conn.h        - public API (S3Connection and related classes). 
  asyncurl.h      - public API for async requests (opaque AsyncMan class).
  s3conn.cpp      - S3Connection implementation. 
  asyncurl.cpp    - AsyncMan implementation.
  sysutil.{h|cpp} - internal helper to abstract platform dependencies.
  s3dbg.cpp       - self-contained unit test (compiled with -DDEBUG).
  s3perf.cpp      - self-contained perf test (compiled with -DPERF).
  s3test.cpp      - command line interface to webstor API.
  
  
  Documentation
  -------------

  webstor public API is instrumented with doxygen comments. 
  To generate documentation, execute: 'doxygen config.doxygen'


  webstor public API 
  ------------------

  The main class exposed by webstor lib is S3Connection. To instantiate it
  set access parameters (Amazon S3 access and secret keys) in S3Config struct
  and pass it to the S3Connection constructor.  After that, call any of
  S3Connection's methods:
  
  - createBucket
  - delBucket
  - listAllBuckets
  - put
  - get
  - listObjects [list with paging]
  - listAllObjects
  - del
  - delAll
  
  A large data set can be uploaded through Amazon S3 multipart upload,
  which is available through the next methods on S3Connection:
  
  - initiateMultipartUpload
  - putPart
  - completeMultipartUpload
  - abortMultipartUpload
  - abortAllMultipartUploads
  - listMultipartUploads
  - listAllMultipartUploads
  
  To use async operations, create an instance of AsyncMan class (effectively
  a singleton) and use the following pairs of S3Connection methods:
  
  - pendPut( AsyncMan *asyncMan, .. ) / completePut( .. )
  - pendGet( AsyncMan *asyncMan, .. ) / completeGet( .. )
  - pendDel( AsyncMan *asyncMan, .. ) / completeDel( .. )  
  
  All pendXX(..) methods start an async operation and return immediately
  without blocking.  To retrieve the result, call the corresponding 
  completeXXX(..) which blocks and waits until the operation completes.
  An application can check the status of an async operation and/or cancel it
  using isAsyncCompleted(..) and cancelAsync(..) calls.

  To start multiple async operations, an application can instantiate multiple
  S3Connection instances and start one async operation on each S3Connection.
  
  AsyncMan instance passed to the pendXX(..) methods manages lifetime
  of background thread(s).  Lifetime of AsyncMan instance must be greater
  than any async operation started with that AsyncMan.
  
  Other async-related methods:
  
  - cancelAsync();
  - isAsyncPending();
  - isAsyncCompleted();
  
  Miscellaneous methods defined by S3Connection:
  
  - setTimeout
  - setConnectTimeout
  - enableTracing [provide callback to access HTTP headers and body]
  
  Structs and helper methods provided by webstor:
  
    struct S3Config
    {
        const char *accKey;      // Access key.
        const char *secKey;      // Secret key.
        const char *host;        // Optional region-specific host endpoint.
        const char *port;        // Optional port name.
        bool        isHttps;     // Indicates if HTTPS should be used for all requests.
        bool        isWalrus;    // Indicates if storage provider is Walrus.
        const char *proxy;       // Optional proxy with port name: "proxy:port".
        const char *sslCertFile; // Optional file name containing SSL CA certificates.
    };
  
  - class S3Bucket                          // a collection of S3Buckets is returned from listAllBuckets(..)
  - class S3PutResponse                     // response from put(..) and completePut(..)
  - class S3PutRequestUploader              // a callback to upload data for put(..) and putPart(..) requests.
  - class S3GetResponse                     // a response from get(..) and completeGet(..)
  - class S3GetResponseLoader               // a callback to download data from get(..) request.
  - class S3DelResponse                     // a response from del(..)
  - class S3Object                          // a collection of S3Objects is returned from listObjects(..)
  - class S3ObjectEnum                      // a callback to enumerate objects returned by listObjects(..)
  - class S3ListObjectsResponse             // a response from listObjects(..)
  - class S3InitiateMultipartUploadResponse // a response from initiateMultipartUpload(..)
  - class S3CompleteMultipartUploadResponse // a response from completeMultipartUpload(..)
  - class S3MultipartUpload                 // a collection of S3MultipartUpload is returned from listMultipartUploads(..)
  - class S3MultipartUploadEnum             // a callback to enumerate uploades returned by listMultipartUploads(..)
  - class S3ListMultipartUploadsResponse    // a response from listMultipartUploads(..)
  - TraceInfo, TraceCallback                // enum and callback for HTTP tracing.
  - setBackgroundErrHandler                 // sets a callback to log and handle exeptions raised on the background thread.
  - dbgSetShowAssert                        // sets a callback to show assert dialog. DEBUG only.
  
  
  Usage example
  -------------
  
   #include "s3conn.h"

   try
   {
       // Instantiate S3Connection.   

       S3Config config = {}; // initialize with 0s.
       config.accKey = AWS_ACCESS_KEY;
       config.secKey = AWS_SECRET_KEY;
       config.isHttps = true;

       S3Connection con(config);
       
       // Execute 'put' request.
       
       const unsigned char data[] = { 0, 1, 2, 3, 4, 5 };         
       con.put(BUCKET_NAME, KEY, data, sizeof(data));
       
       // Execute 'get' request.
       
       unsigned char buf[16];
       S3GetResponse response;
       con.get(BUCKET_NAME, KEY, buf, sizeof(buf), &response /* out */);
       
       // Verify results.
       
       ASSERT(!response.isTruncated);
       ASSERT(response.loadedContentLength == 6);
  }
  catch(const std::exception& e)
  {
       std::cout << "Exception: " << e.what();
  }
  
  
  s3test
  ------
  
  s3test is a command line interface to webstor API.

  It accepts the following parameters:
  
      -i mandatory AWS access key,                                               
         (it can be specified via AWS_ACCESS_KEY env. variable)                  
      -s mandatory AWS secret key,                                               
         (it can be specified via AWS_SECRET_KEY env. variable)                  
      -H optional region-specific endpoint or a mandatory Walrus host name,      
         (it can be specified via AWS_HOST env. variable)                        
      -P optional port number,                                                   
      -U (optional flag to use HTTP instead of HTTPS),                           
      -G optional proxy with port number (proxy:port),                           
         (it can be specified via AWS_PROXY env. variable)                       
      -a action, one of the following:                                           
         createBucket                                                            
         delBucket                                                               
         listAllBuckets                                                          
         put                                                                     
         get                                                                     
         del                                                                     
         delAll (delete by prefix)                                               
         listAllObjects                                                          
         listAllMultipartUploads                                                 
         abortAllMultipartUploads                                                
                                                                                 
      action-specific parameters, some of them mandatory depending on action:    
                                                                                 
      -f filename (for 'put' and 'get'),                                         
      -n bucket name (all except for 'listAllBuckets'),                          
         (it can be specified via AWS_BUCKET_NAME env. variable)                 
      -p key or key prefix (all except for bucket-related actions),              
      -m marker for entries to list (for 'listAllObjects' and                    
         'listAllMultipartUploads'),                                             
      -x maximum number of entries to list (for 'listAllObjects' and             
         'listAllMultipartUploads'),                                             
      -d delimiter to list directories (for 'listAllObjects' and                 
         'listAllMultipartUploads'),                                             
      -z chunk size in MB (for 'put' to enable multipart upload,                 
         size must be 5MB minimum, not supported by Walrus),                     
      -b make public (for 'createBucket' and 'put'),                             
      -v verbose mode.                                                           
                                                                                 
  Some of options can be specified through env. variables:                       
      AWS_ACCESS_KEY  - instead of option '-i',                                  
      AWS_SECRET_KEY  - instead of option '-s',                                  
      AWS_HOST        - instead of option '-H',                                  
      AWS_BUCKET_NAME - instead of option '-n',                                  
      AWS_PROXY       - instead of option '-G',                                  
                                                                                 
  Notes:                                                                         
      If you specify '-z' flag and upload doesn't finish because of crash or     
      connection issues, orphan chunks can be left in Amazon S3 storage.         
      It's recommended to execute 'listAllMultipartUploads' and                  
      'abortAllMultipartUploads' actions to purge them.                          
                                                                                 
  Examples:                                                                      
                                                                                 
   * create a new bucket:                                                        
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a createBucket -n mybucket      
                                                                                 
   * delete a bucket:                                                            
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a delBucket -n mybucket         
                                                                                 
   * list all buckets:                                                           
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a listAllBuckets                
                                                                                 
   * upload a file:                                                              
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a put -n mybucket               
     -f image.jpg -p folder/image.jpg -v                                         
                                                                                 
   * upload a large file using multipart upload:                                 
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a put -n mybucket               
     -f image.jpg -p folder/image.jpg -z 5 -v                                    
                                                                                 
   * download a file:                                                            
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a get -n mybucket               
     -f image.jpg -p folder/image.jpg -v                                         
                                                                                 
   * delete an object:                                                           
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a del -n mybucket               
     -p folder/image.jpg                                                         
                                                                                 
   * delete all objects for a given prefix (e.g. from a directory):              
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a delAll -n mybucket -p folder/ 
                                                                                 
   * list all objects:                                                           
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a listAllObjects -n mybucket    
                                                                                 
   * list all objects with a given prefix (e.g. all objects in a directory):     
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a listAllObjects -n mybucket    
     -p folder/                                                                  
                                                                                 
   * list all top-level directories:                                             
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a listAllObjects                
     -n mybucket -d /                                                            
                                                                                 
   * list all sub directories:                                                   
     s3test -i AWS_ACCESS_KEY -s AWS_SECRET_KEY -a listAllObjects -n mybucket    
     -p folder/ -d /                                                             
  
  
  Bugs and errors
  ---------------

  Bug reports should be sent to bugs@oblaksoft.com


  Contacts
  --------

  Please refer to www.oblaksoft.com website for webstor library updates
  as well as other news about OblakSoft products and releases.

