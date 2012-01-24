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

Each node specifies a certain amount of 'upload bandwidth' that they desire to
sell to the network.  Combined with this target bandwidth, there is also
a target for disk usage.

Each chunk has two properties, distance and popularity.

Each node will determine how 'far' (range) it will serve chunks for and
also the 'min popularity' of the chunks it will serve.

A node with small storage (100MB) and large bandwidth will want to increase its range until the
sum of popularity (frequency) the top 100MB of the most popular items equals the available bandwidth.


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




