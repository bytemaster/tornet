Introduction
----------------------------------
Tornet is an attempt to completely decentralize the hosting and distribution
of content including web pages, video streams, email, chat, and voice in a manner
that protects freedom of speech and is resistant to attack.

Imagine combining the best features of I2P, TOR, Freenet, Namecoin, Bitcoin, Bittorrent,
VUZE and IRC into a new P2P network where users have financial incentive to contribute 
resources to the network.  This is the ultimate in cloud computing.

The primary feature of Tornet that will set it apart from everything that has gone
before it is the economic incentive to contribute to the network.  Therefore, each node
in the network is operating on a for-profit instead of a non-profit basis.  The ability
to make a profit is made possible via Bitcoin which will be used to anonymously tip 
hosts that provide resources to the network.

Technical Approach to Overlay Network
----------------------------------
To keep things anonymous and secure, all traffic is encrypted.
To traverse NATs and handle large numbers of connections, all data is sent over UDP.  
To keep CPU load down, the effecient 'blowfish' algorithm is used.
  - it is good enough to prevent deep packet inspection
  - it changes frequently enough make it econimcally unprofitable to crack it.
  - really sensitive content is encrypted separately
To enable effecient routing a modified kademilia routing algorithm is used.
To prevent targeted attacks or impersonation, a node is identified by the hash of its public key (2048 bit).
To prevent Sybil attacks and encourage long term establishment of node identies, each
  node is ranked by hashing its public key with a nonce, the lower the resulting hash
  the higher the rank.   It requires significant time/cpu resources to generate a
  high-ranking identity.

To optimize 'routing' nodes within a kbucket (same distance) are sorted by:
    - how much content they have provided relative to other peers
    - how much btc they have paid relative to other peers
    - rank  
    - how long the node has been connected
    - how low the latency is
    - how high the bandwidth is.

To provide extensability, each connection multiplexes packets over channels which
  communicate to registered services.
To provide high-performance gauranteed in-order delivery, some channels implement 
the UDT protocol.
To provide unique name registration, Name Coin will be used.

Economic Incentive
--------------------------------------
Price negotiation is 'expensive', especially if you must negotiate with 1000's of 
peers and the market prices change frequently.  As a general rule, most users can
pay 'in-kind' by uploading as much or more than they download; however, some nodes have
higher demand or are unable to serve files (because they are behind a firewall).  Other 
services are 'asymentric' such as routing, tunneling, etc and therefor you require more 
resources from a specific node than that node requires of you.  Some users
simply want higher speeds and lower latencies.  

Therefore, each user simply picks an amount to donate to the network.  This donation is 
then divided among all peers proportional to the amount of service they have provided.   
The result of donations is higher ranking in the priority queue and therefore faster
network speed and lower latencies. 

Each peer extends credit to 'new peers' proportional to their rank and past payment
history.  Because credit is tied to 'rank' and rank requires cpu time / money to aquire it
is not profitable to constantly create new IDs as new IDs have the lowest priority.

An ID that doesn't contribute resources or BTC and uses up its credit will eventually find
the network unusable.  

Services built ontop of Overlay Network
--------------------------------------

  - Distributed File System 
  - Distributed Key Value Store
  - Distributed Email System
  - TOR-like Onion Routing
  - I2P-like Hidden Services 


Distributed File System
-------------------------------------
An analogy can be made between a spinning hard disk and a distributed file system in that
seek-times can be much slower than sequential read times.  Often files are small and the
network costs of finding the file can signficantly exceed the cost of transfering the file 
once found.

To make the most effecient use of network bandwidth, small files are grouped together into
chunks.  To address one of the files you need to know the chunk ID the start location and
the size of the file.

Some files are too big to fit into a single chunk, these files are divided among multiple chunks and
a header is stored that describes how to re-assemble the file.

A directory is just file that contains a mapping from a name to a file reference.  

At the end of the day, we want a content addressable file system and to avoid storing duplicate
data if at all possible. So we will refer to all files by the sha1() of their contents and then
have a mapping of sha1(contents) to chunk+seed+offset+size.  So a directory file will look like
this:

    struct directory {
      struct cname_to_content {
        uint128 name_hash;
        sha1    content_hash;
      };
      struct entry {
        sha1      chunk;
        uint64_t  seed;
        uint32_t  start; 
        uint64_t  length;
        uint32_t  name_pos;
      };
      uint64           numfiles;
      cname_to_content files[numfiles];
      entry            entries[numfiles];
      vector<string>   names;
    }

Everything is 'fixed-length' except the names which are stored at the end.  This enables us
to easily index a directory without having to deserialize the whole thing.

For each chunk we need to store a hash for every 32KB sub-chunk to allow partial retrieval.

Each host can then maintain a 'file cache' with the decoded files so that if a directory 
changes that it can still identify the files that are 'unchanged'.  

If a file must be split among chunks, then it has a header that describes this split, otherwise,
small files always fall within a given chunk.  Editing a single file should not cause subsequent
runs of the same archive function to invalidate the entries for all subsequent files.  

A user publishing a directory would maintain a DB of hash to entry and refer to this DB when packing
a directory so that only new files are put into new chunks.  This will help optimize the caching on 
the network by preventing small changes from 'cascading'.


Files come in many sizes, but often they are can be orders of magnitude smaller than the
network traffic required to locate the file.  To resolve this issue 



Files are distributed across the network in 1MB (or less) chunks.  Each chunk is encrypted via
blowfish using the hash of the unencrypted file as the key; therefore, you must know the
unencrypted hash before you can decrypt the chunk and you must know the hash of the encrypted
chunk to find it on the network.

Each node has a financial incentive to extract the most value out of its limited bandwidth 
and disk space.  A node with limited bandwidth but large disk storage would want to store 
'rarely' accessed files that the users looking for them will pay a 'premium' for.  A node
with unlimited bandwidth, but limited disk space will want to store files in high-demand until
the demand for their smaller set of files equals the available bandwidth.

At the same time that nodes want to optimize profitability, we want to ensure that nodes
keep content close to their ID.  The closer a chunk is to a node's ID the more value that
node realizes for providing it.  This means that instead of optimizing on access frequency,
bandwidth, and storage each node has more incentive to offer chunks near its ID than far from 
its ID.  

The side effect of the above relationship is that each user is incentivized to find the chunk
on a node furthest from the chunk ID.   This encourages the use of 'binary kad search' of the
network and it is this binary search that allows nodes on the network to estimate the popularity
of content and then opportunistically cache content that would be 'profitable' for it to host
based upon access frequency and distance from the node ID.

On the other hand, nodes that are willing to pay a 'high price' for low-latency can short-circuit 
the lookup process and start their query much closer to the target node.  This short-circut of the
lookup process 'harms' the network by hindering the ability of nodes further away to cache the
content.  If every node did this then they could DOS the target node.  Fortunately, because it is
more expensive it naturally self-limiting.  

Publishing Content
------------------------------------
The cost of publishing content on a node is proportional to opportunity cost of that
node giving up a slot in its file cache for your content.  After all, nodes are in this
for profit so each node can multiply the access frequency for a chunk by the yield of that
chunk and the determine the expected revenue per-week.  A node must 'bump' this chunk in order
to pubish the content.

Furthermore, each node can only publish on nodes of 'lower rank', therefore new users / IDs end up
being 'beta-testers' for content and nodes that wish to publish to more reliable nodes must
invest in their identity.  Market forces will then ensure that 'good' content is kept, and 
'bad', unused, or outdated content is dropped.   Due to the large number of users with small
upload speeds and large harddives, there should be significant incentive for them to speculatively
store infrequently accessed chunks.

Distributed Key Value Store
-----------------------------------------
Every ID doubles as a namespace in which key/value pairs may be published on the network.  Therefore, each
node can store 'small values', under 1KB mapped to keys under 256B on nodes near the hash of the key.  The
publishing of key/value pairs is subject to the same market principles as the content addressable storage.

Name Registration Lookup
-----------------------------------------
To register a name you must first calculate a difficult hash and then broadcast your registration to the
network.  Your broadcast will include a public key which will identify you as the new owner of that name until
it expires.  You must rebroadcast your name registration every month, and by referencing your prior registration
you will be able to build up a reputation for a given name.

It will be possible to assign a human readable name to an ID via the use of Namecoin.  The combination of
Namecoin, key/value store, and the distributed filesystem provides everything necessary to implement a
distributed static internet that can be browsed much like the internet of the early 1990's.   

Distributed Email
-----------------------------------------
To receive a message, a mailbox is created using the hash of its public key.  This mailbox may be created on
one or more nodes.  By creating the mailbox you are informing others that you will be 'checking this node' for
mail.

To send a message you must first know the public key of the user you wish to mail.  This can be discovered via
namecoin and/or the key/value store.

You compose your message complete with any attachements, then archive, compress, and encrypt it with the public
key and finally you publish the message to the web like any other file chunk.  Then you find one or more nodes
that are hosting the mailbox for destination ID and push the message hash into the inbox along with a nonce that
demonstrates proof of work.  The proof of work must be unique and include a token provided by the mailbox host that
prevents spammers from publishing bogus hashes that send receviers on fishing expiditions looking for non-existant
messages.  

Because nodes can come and go at will, the sender of a message may send it to redundant mailboxes and ensure that
the body of the message is availble on multiple nodes. (paying for storage).

Upon receiving the message, the recevier signs the hash and then pushes the result back to the senders inbox to allow
the sender to 'stop publishing' the content.

Distributed Multicast Streaming
-------------------------------------------
Suppose a user wanted to create an internet radio station. They would create a public key for the station and then
find one or more nodes to 'broadcast to' that are near that ID and then start 'streaming' content to that node.

Someone wanting to listen to the stream would then start a kad-search looking for the stream and subscribe to the
first node who has the stream.  That node will then earn income from both the publisher and subscriber.

In the process of 'searching' for a stream, other nodes will discover the demand (via unanswerable queries), and based
upon the demand choose to subscribe to the stream themselves.  The result is a distribution tree.  Because the 'cost' of 
subscribing to a stream goes up the closer to the source a user is, users will be motivated to subscribe to leaf nodes 
instead of the source nodes.  The cost of subscribing to a stream also grows with 'demand' on a given node causing clients
to automatically 'load balance' among nodes.

In this way enough bandwidth should be available to allow anyone to multi-cast a video stream to the entire network in 
the most economically effecient manner possible.

This same techinque can be used for distributed twitter, IRC, and the like.


Performance
---------------------------------------
Lets assume a standard linux distribution 1GB divided into 1024 1MB chunks and with 1M nodes
with no overlap in data, then you will have an estimated 40KB 'search' cost per 1MB of found
data.  This will result in a 4% search overhead worst case (log(1M) search hops).  Popular files 
are likely to be cached far from the leaf and therefore reduce overhead by a significant margin. 

There is no reason to divide chunks into smaller pieces because clients can request sub-chunks
from multiple different nodes.  Therefore, a search that yields 3 nodes hosting a particular
chunk can download a different part of the same chunk from each node.

With 1 million nodes and log2 lookup performance and 0 overlap it would take 20 hops to find
a rare chunk.  At 0.5s average latency, that could be up to 10 seconds.  If you assume 500% 
overlap (each chunk is hosted by at least 5 nodes then your lookup time improves by 2.5 seconds  
or 7.5 seconds for the 'least popular' data.  Every time popularity doubles, it shaves 0.5 seconds
off of the lookup time.  A chunk that is 16,000 times as popular as the 'least popular' data should
be found in as few as 1 or 2 hops (less than 1 second).  

Therefore, for browsing the 'web' it should perform reasonably well for popular sites which require
1 initial lookup for the page data.  Latency of multiple lookups would be hidden in large files through
pipelining. 

Clearly if a large number of nodes are hosting content on fast links with low latency (which
there is financial incentive to do so), then you can expect latency to drop from an average of
500ms to 50ms for most queries (like pinging google).  This would reduce the 'worst case' 
lookup time to 1 second from 10 seconds and the best case time will be similar to DNS lookups.

Because the KAD algorithm can allow you to perform lookups in 'parallel', your latency will be the
best of 3 (or more nodes) which further increases performance.

Note that latency for a given node depends upon that nodes priority and priority is based upon
its contribution to the network in terms of both bandwidth, disk space, and bitcoins.  Freelaoders 
will experience higher latencies than high-paying customers. 

Another way users can boost performance is to delegate 'lookup requests' to another node that is
closer to the internet backbone than your home DSL link. This could shave 10's of ms off of every
hop in the search.  This would concentrate your debt to one node instead of spreading it over 
a bunch of nodes, but would also give both you and the 'proxy' improved 'privacy' masking what
you are searching for.

Keeping things Anonymous
-----------------------------------------
By default the cheapest and highest performance system exposes your IP/PORT and query to
every node in the search path.  This would allow potential attackers to participate in the network
and gather stats about who is looking for and downloading what.  To get around this the system can
set up 'tunnels' that route your requests through one or more nodes.  The longer the tunnel the more
it will 'cost'.  This allows users who value privacy to 'pay for it' while also providing a smoke
screen of 'plausable deniability' to other users who get to mix their 'direct' request in with 
indirect requests.  

In this way no one can 'prove' who published or downloaded what.  No one can prove who is using tunnels and
who is not.  


Search Engine
-----------------------------------------
Because every chunk has a certain 'popularity' a search engine that is 'crawling' the
web can 'rank' pages/files by their popularity.  

A search engine would simply publish its 'index' as a set of chunks that any other node/user
can download. Why would anyone consume significant resoruces to generate an index only to
give it away?  Perhaps to help drive more traffic to their servers?  The easier it is to find
content the more content will be downloaded.  

The other alternative is hidden services.  These services allow anonymous hidden servers to aggregate
user content and 'republish' static pages with the result.  A search engine could then generate a
'results page' for every possible chunk and then when a user 'searches' they get the results page
instead of the index.  This page could then imbed ads based upon the search term.  


Marketability
-----------------------------------------
Why would your average internet user want to switch from the existing internet to TORNET? 
  - Earn income from unused bandwidth 
  - Higher download speeds
  - No censorship
  - Privacy





_______________________
OLD OUTDATED IDEAS

Chunk Lookup
----------------------------------
Each node specifies a certain amount of 'upload bandwidth and storage' that they desire to
sell to the network.  

Each chunk has two properties, distance from node ID and popularity.

Each node will determine how 'far' (range) it will serve chunks for and
also the 'min popularity' of the chunks it will serve.

A node with small storage (100MB) and large bandwidth will want to increase its range until the
sum of popularity (frequency) the top 100MB of the most popular items equals the available bandwidth.

There needs to be a motivation for storing data near the user instead of simply hosting the most
popular data at the expense of lesser requested data.  Without this no one would be motivated to
contribute disk space.  So when accounting is performed, a user must factor in the 'distance' to
the chunk in question.   You get less credit for a chunk far from your ID than one near your ID.

Distance from node to chunk is  log2( NID ^ CID ) will tell you the number of significant bits in
the distance with a max distance of 160 and min distance of 0.  So when someone downloads a chunk you
charge them BYTES * (160-LOG2(NID^CID)).  This has the effect of motivating the downloader to grab
the chunk from as far away as possible from the source to get cheaper 'cached' copies rather than the
more expensive directly sourced chunks.

To maximize 'credit' the server wants to calculate the 'return per chunk' based upon its query 
interval and distance. 


Publishing Chunks
-----------------------------------
A server is only interested in publishing your data if you pay enough to bump the least profitable
chunk for 1 weak AND your identity is greater than the server's.


Server Rules
-----------------------------------
Expand chunk range until desired bandwidth is consumed.
If disk space runs out, drop least frequently accessed data, preferring most popular data.

- Due to the cost of 'aquiring' data in the first place, a node will generally store more data than it is
currently honoring requests for.  This allows the system time to respond quicker to changes in 
network access pattern.


Chunks are not pushed onto the network, they are pulled based upon demand. 
Chunk references can be pushed into the network for chunks hosted at non-normalized location.

In this way no one can 'flood' the network with garabage data.



Selling Service
------------------------------------
Each node mantains a 'balance' / line-of-credit with all other peers.
Some users are net sellers and others are net buyers.

A user who downloads a lot of content will, statistically, make an equal number of
queries to all other nodes in porportion to each nodes available upload bandwidth.

A coorelary to this is that other users will statistically make requests of you porportional
to your upload bandwidth.

If all users are equal then the bandwidth usage will average out and you will be allowed to
download as much as you upload to the network.

Some nodes may not want to download anything at all, but instead want to sell their service.  These nodes
will sell their capacity once the initial line of credit has been used.

Let us presume that the maximum credit line any node will extend to any other node is 100 MB * RANK.

That node will then set a price, in bitcoin, that once paid will reverse the credit.



Identity Spoofing
-------------------------------------
One tactic that an individual could use to 'steal' content is to create and throw away
their identity after they have used up their credit with other nodes.  This issue can be
mitigated by having the user produce a proof-of-work on their identity.  This proof of work
effectively shows how much someone has 'invested' in their identity.  Each node can require
a minimum level of 'work' before they will even talk to another node.  The market can then
determine the relationship between the identity and line of credit.

You can only 'advertize' on nodes of lower-rank than yourself.  Thus publishing data requires
a larger investment in your identity than downloading data.


Potential Attacks
--------------------------------------
* Query a bunch of random, non-existant chunks to force nodes to attempt to cache these
chunks.  
   - if chunk is searched for and not found, increment a 'not found' count 
   - when other nodes are searching for a chunk, return the not found count
   - this allows other nodes to quickly learn when a resource has already been searched for
      but not found and therefore they can stop searching sooner.  
   - this still has the potential problem of maintaining meta info on a bunch of bogus chunks.
   - this is a good argument for keeping things 'closed source'.  


   - mitigated by checking user rank, charging per-request, only logging requests from ids
    with a known reputation.  
   - resetting the query count if chunk is not found
   - requiring enough 'paid for' requests to cover the cost of finding the chunk
   - nodes should know most of their neighbors and thus reduce the 'search radius' for
   the desired chunk dramatically.


File Description
------------------------------------------
A file is described as a series of chunks identified by the sha1(data) of the data.  Each
chunk has a size and a list of 64KB slices identified by a superfast hash(slice).  These 
slice hashes can be used to verify partial chunk downloads from multiple nodes. 



P2P Tagging
-----------------------------------------
Each node may 'tag' individual tornets with words and then publish the tags in
a KAD key/value database.  These tags should also be published on the node that
hosts the chunk. Each tag is signed by the tagger and this signature is used to 
track their reputation.   

Design
-----------------------------------------
Each node maintains two chunk databases, local and cache.  
  - local stores chunks used by this client
  - cache stores chunks opportunistically cached for profit.

Each node further maintains a directory containing tornet files.
  - a tornet file describes how to assemble chunks into a file.

Each node maintains a database of tornets that it is publishing
  - for each chunk maintain a list of N nodes known to host it
  - check each chunk once per hour and 're-publish' if necessary
  - popular content should automatically remain and 'spread', rare
    content may need someone to continually pay for the data to
    be hosted.
  - when checking the status of a chunk, the publisher also gathers
    access rate stats that is useful for knowing how popular individual
    chunks are.

Each node maintains an account for all other nodes maintaining the following information
  - node id              - sha1(public key)  (primary key)
  - public key           - used to validate node id
  - nonce                - used to determine rank, (161-sha1(nonce+public key).log2())
  - first contact time   - when combined with last contact time
  - last contact time    - dead contacts may be deleted
  - RTT                  - Used to enhance routing
  - last endpoint        - IP:PORT this node was last seen at  (indexed)
  - total sent credit    - factor in storage rates for data provided
  - total recv credit    - factor in storage rates for data recv
  - send btc addr        - address used to send btc to node
  - recv btc addr        - address used to recv btc from node
  - connection errors    - when combined with first and last contact time yield an average availability
  - DH Key               - the last key exchange for the given endpoint, used to re-establish encrypted coms






