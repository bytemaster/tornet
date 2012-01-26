Overview
----------------------------------

This library provides the foundation of a P2P framework based upon UDP
message passing and remote procedure calls.  A UDT like protocol is layered
on top of UDP to provide flow control and retransmitting.  

What makes tornet different from other P2P systems is the utilization of
a digital currency to track contributions to the network and allow anonymous
users to purchase service.

All traffic is encrypted to prevent deep packet insepction.

This software is still under heavy development.


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
   - mitigated by checking user rank, charging per-request, only logging requests from ids
    with a known reputation.  
   - resetting the query count if chunk is not found
   - requiring enough 'paid for' requests to cover the cost of finding the chunk
   - nodes should know most of their neighbors and thus reduce the 'search radius' for
   the desired chunk dramatically.


Performance
---------------------------------------
Lets assume a standard linux distribution 1GB divided into 1024 1MB chunks and with 1M nodes
with no overlap in data, then you will have an estimated 40K 'search' cost per 1MB of found
data.  This will result in a 4% search overhead worst case.   Popular files are likely to
be cached far from the leaf and therefore reduce overhead by a significant margin. 

There is no reason to divide chunks into smaller pieces because clients can request sub-chunks
from multiple different nodes.  Therefore, a search that yields 3 nodes hosting a particular
chunk can download a different part of the same chunk from each node.

With 1 million nodes and log2 lookup performance and 0 overlap it would take 20 hops to find
a rare chunk.  At 0.5s average latency, that could be up to 10 seconds.  If you assume 500% 
overlap (each chunk is hosted by at least 5 nodes then your lookup time improves by 2.5 seconds  
or 7.5 seconds for the 'least popular' data.  Every time popularity doubles, it shaves 0.5 seconds
off of the lookup time.  A chunk that is 16K times as popular as the 'least popular' data should
be found in as few as 1 or 2 hops (less than 1 second).  

Therefore, for browsing the 'web' it should perform reasonably well for popular sites which require
1 initial lookup for the page data.  Latency would be hidden in large files.  

For paying customers, they can simply use a 'super cluster' which would cache everything or a
master index that will perform as fast as a DNS lookup.


File Description
----------------------------------------
A file is described as a series of chunks identified by the sha1(data) of the data.  Each
chunk has a size and a list of 64KB slices identified by a superfast hash(slice).  These 
slice hashes can be used to verify partial chunk downloads from multiple nodes. 



