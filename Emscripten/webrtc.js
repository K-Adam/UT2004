// The browser side of IpDrv/Src/WebRtcBrowser.cpp, linked with --js-library.
// The 'p' in each __sig makes Emscripten hand wasm64 pointers over as Numbers.

addToLibrary({
  $UTWebRtc: { peers: new Map(), next: 1 },

  ut_webrtc_connect__deps: ['$UTWebRtc'],
  ut_webrtc_connect__sig: 'ippi',
  ut_webrtc_connect: (answer, ufrag, maxInbox) => {
    answer = UTF8ToString(answer);
    ufrag = UTF8ToString(ufrag);
    const peer = { inbox: [], closed: false };
    const id = UTWebRtc.next++;
    UTWebRtc.peers.set(id, peer);
    const fail = (e) => {
      err(`WebRTC: ${e}`);
      peer.closed = true;
    };
    try {
      peer.pc = new RTCPeerConnection();
      // Negotiated as channel 0 on both ends, unordered and unreliable like UDP.
      peer.dc = peer.pc.createDataChannel('', { negotiated: true, id: 0, ordered: false, maxRetransmits: 0 });
      peer.dc.binaryType = 'arraybuffer';
      peer.dc.onmessage = (e) => {
        if (peer.inbox.length < maxInbox) peer.inbox.push(new Uint8Array(e.data));
      };
      peer.dc.onclose = () => { peer.closed = true; };
      peer.pc.onconnectionstatechange = () => {
        if (peer.pc.connectionState == 'failed' || peer.pc.connectionState == 'closed') peer.closed = true;
      };
      // The server learns the ufrag from the first STUN request, so the offer carries the one its answer was written for.
      peer.pc.createOffer()
        .then((offer) => peer.pc.setLocalDescription({
          type: 'offer',
          sdp: offer.sdp.replace(/a=ice-ufrag:.*\r\n/g, `a=ice-ufrag:${ufrag}\r\n`).replace(/a=ice-pwd:.*\r\n/g, `a=ice-pwd:${ufrag}\r\n`),
        }))
        .then(() => peer.pc.setRemoteDescription({ type: 'answer', sdp: answer }))
        .catch(fail);
    } catch (e) {
      fail(e);
    }
    return id;
  },

  ut_webrtc_send__deps: ['$UTWebRtc'],
  ut_webrtc_send__sig: 'vipi',
  ut_webrtc_send: (id, data, count) => {
    const peer = UTWebRtc.peers.get(id);
    if (peer?.dc?.readyState == 'open') peer.dc.send(HEAPU8.slice(data, data + count));
  },

  ut_webrtc_recv__deps: ['$UTWebRtc'],
  ut_webrtc_recv__sig: 'iipi',
  ut_webrtc_recv: (id, data, max) => {
    const peer = UTWebRtc.peers.get(id);
    if (!peer) return -1;
    let msg;
    // Larger than any game packet, so not from the server.
    while ((msg = peer.inbox.shift()) && msg.length > max) {}
    if (!msg) return peer.closed ? -1 : 0;
    HEAPU8.set(msg, data);
    return msg.length;
  },

  ut_webrtc_close__deps: ['$UTWebRtc'],
  ut_webrtc_close__sig: 'vi',
  ut_webrtc_close: (id) => {
    UTWebRtc.peers.get(id)?.pc?.close();
    UTWebRtc.peers.delete(id);
  },
});
