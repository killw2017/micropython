# Simple test creating an SSL connection and transferring some data

try:
    import binascii
    import socket
    import ssl
except ImportError:
    print("SKIP")
    raise SystemExit

PORT = 8000


# This self-signed key/cert pair is randomly generated and to be used for
# testing/demonstration only.  You should always generate your own key/cert.

# To generate a new self-signed key/cert pair with openssl do:
# $ openssl req -x509 -newkey rsa:4096 -keyout rsa_key.pem -out rsa_cert.pem
# -days 365 -nodes
# In this case CN is: micropython.local
#
# Convert them to DER format:
# $ openssl rsa -in rsa_key.pem -out rsa_key.der -outform DER
# $ openssl x509 -in rsa_cert.pem -out rsa_cert.der -outform DER
#
# Then convert to hex format, eg using binascii.hexlify(data).


cert = binascii.unhexlify(
    b"308205b53082039da00302010202090090195a9382cbcbef300d06092a864886f70d01010b050030"
    b"71310b3009060355040613024155310c300a06035504080c03466f6f310c300a06035504070c0342"
    b"617231143012060355040a0c0b4d6963726f507974686f6e31143012060355040b0c0b4d6963726f"
    b"507974686f6e311a301806035504030c116d6963726f707974686f6e2e6c6f63616c301e170d3233"
    b"303731353136323034395a170d3238303731333136323034395a3071310b30090603550406130241"
    b"55310c300a06035504080c03466f6f310c300a06035504070c0342617231143012060355040a0c0b"
    b"4d6963726f507974686f6e31143012060355040b0c0b4d6963726f507974686f6e311a3018060355"
    b"04030c116d6963726f707974686f6e2e6c6f63616c30820222300d06092a864886f70d0101010500"
    b"0382020f003082020a0282020100944fdb40b587af0cf7e9696c355d24a70936874e6a3bd2598166"
    b"ce2495aaf9b4af01b54471f7cbf3626ae0720bf0bfd520507f79ec553c62898bfd2598385f56061b"
    b"0e8f452625c82d3c83e2a0d070ab9be2db21faf88c58e4a61d62f8ff43960aa1ffdadaad41f7cb2e"
    b"b337070a39f08ff9fe20c09b19926cbbc4a5154b796ff7e7ce11334e090d360c81072af08758f6cd"
    b"7bad75bc7b95b6dcc801c85de81d72806ca3ce0782bfcbdffce707f9fb1572a7db0d74445dc32d5f"
    b"bea12a3ab1d47edf668ebfa60ed8b51e654e76292e3894ee574ea851064956906aa8afe00e67664e"
    b"110b5a6ff7db51f7944463cdd626ff2ec7886c229f4ca5985168f20f8f210972b5ff9181d4f3beb8"
    b"914ec5b24a0953253b3d42ab55e98bd70cb25e7a24c603b27ec83e1ce31c90b728b47a5f606ff2a1"
    b"0ff784a016894c28f7e71f51a78b0a7601bbbc8c1b132b04e567394a327a7aa4674e8e4c0bfaec4b"
    b"eeccf0ed09d1660933d718a2f34ff91d79d875a73fbac07182a9531ca52bd360e2678f95ff9b4ba2"
    b"1490d7456548364b2eb335c207d6e1e48ccd7d8cb43868a334c095bd9673be7403f3b69b545ee904"
    b"a3f513d2b2a2dd46f06820cd394819551dd05d9b34a8a3238a521f6c1c3592f76d5ef29e181c60ee"
    b"bcaf4c63098794c15d4f82e7425e75ff8f5430247ecc0e7f2983b715506012f187d54a7b6729bc61"
    b"fa4d10a9f22b0203010001a350304e301d0603551d0e041604147a6d126931b58fa1c3dff3c9215f"
    b"6202e61fa8da301f0603551d230418301680147a6d126931b58fa1c3dff3c9215f6202e61fa8da30"
    b"0c0603551d13040530030101ff300d06092a864886f70d01010b0500038202010051b3a4834d2bf1"
    b"95ac645bca61e95289eff38d80ab2ee732c6ebd1370781b66955a7583c78831f9fb7d8b236a1b5ff"
    b"c9183b4e17225951e1fb2c39f7218af30ec3fd8e4f697e0d10ecd05eb14208535dc55bc1e25d8a43"
    b"050670d4de3e4cb8c884e6cbb6b884320d496b354acf5258bcb0ddaefd065ee8fccbddf3a2bfa10d"
    b"bfeb8ab6b2580b50f0678760599269b612f81ba1310bfcd39427fec49211769c514cdd0305081d8a"
    b"11ebe705496d4dcc31ac9fab96a2d298ee4423789baffbfa0fa82ee1b5113f9cf597647a36640cad"
    b"abf535205c322e16153d6ab04b0817f57d8a9a6ca2db2ab10986ae9eab343547e52c78a641868bb5"
    b"e2981182fcc55d86cdc6aa8478b226318a3be72fb726dd0b90f30df810c4d6c6b5a0ecb3c6cc375b"
    b"8d3d244a07d8517ad390929be7b75f679beb63d8c1028905af2383144a4ed560e45907d301846acc"
    b"9dbec86bcdd7fbf8a805b59f359c8bd997f5eb7b8aea6f7a538f9663ec2c12e07d4b37650e92b783"
    b"74356daee4a501eeb27fef79b472b2fcce4363a9ff4d80f96a3b47dc4c4ef380ef231d193a517071"
    b"b31078fa9f9a80cfd943f7e99e4ed8548c9ea80fd845ecc2c89726be273fa8b36680d645998fd1e6"
    b"2367638f4953e9af68531aedb2ee49dffaaed07a4a5b97551712058219ac6f8da71710949f761271"
    b"5273a348dcce40c556bdab00a4ae3a7b23a5934ac88b7640df"
)

key = binascii.unhexlify(
    b"308209280201000282020100944fdb40b587af0cf7e9696c355d24a70936874e6a3bd2598166ce24"
    b"95aaf9b4af01b54471f7cbf3626ae0720bf0bfd520507f79ec553c62898bfd2598385f56061b0e8f"
    b"452625c82d3c83e2a0d070ab9be2db21faf88c58e4a61d62f8ff43960aa1ffdadaad41f7cb2eb337"
    b"070a39f08ff9fe20c09b19926cbbc4a5154b796ff7e7ce11334e090d360c81072af08758f6cd7bad"
    b"75bc7b95b6dcc801c85de81d72806ca3ce0782bfcbdffce707f9fb1572a7db0d74445dc32d5fbea1"
    b"2a3ab1d47edf668ebfa60ed8b51e654e76292e3894ee574ea851064956906aa8afe00e67664e110b"
    b"5a6ff7db51f7944463cdd626ff2ec7886c229f4ca5985168f20f8f210972b5ff9181d4f3beb8914e"
    b"c5b24a0953253b3d42ab55e98bd70cb25e7a24c603b27ec83e1ce31c90b728b47a5f606ff2a10ff7"
    b"84a016894c28f7e71f51a78b0a7601bbbc8c1b132b04e567394a327a7aa4674e8e4c0bfaec4beecc"
    b"f0ed09d1660933d718a2f34ff91d79d875a73fbac07182a9531ca52bd360e2678f95ff9b4ba21490"
    b"d7456548364b2eb335c207d6e1e48ccd7d8cb43868a334c095bd9673be7403f3b69b545ee904a3f5"
    b"13d2b2a2dd46f06820cd394819551dd05d9b34a8a3238a521f6c1c3592f76d5ef29e181c60eebcaf"
    b"4c63098794c15d4f82e7425e75ff8f5430247ecc0e7f2983b715506012f187d54a7b6729bc61fa4d"
    b"10a9f22b0203010001028202000b41080520013cc242299f0b4bfd5663aa6a4dd8206d8ba7a90f11"
    b"036babfea8bc42e7eb5aae8ff656f87f3188406b7e13a6a815ab5e4867bdc236a25caba26857ac43"
    b"ed9134b4d73cbf83ce759f7b7d3a25fbb4d76376dae3f6caf210ace60703a58951a51852922803d2"
    b"2b91c82fdf563d85101d2d67c259a7e1e318fb922a71e85015b40beed9e6c90a1d6e1fb45586dcce"
    b"ceb9c964a356ade82b6275e5c01e492a753f940852df788eab454aadc7d1dc74ddcf7dc493a3e4c9"
    b"0557bbfe747e701b4b27b5c518a29dbcd8385525a1bb835e72a489096e15387e2f70b112c6bbd79e"
    b"a97ae2562f7947cd2367635e25b5656a54aac7f1c892243dc135e5025a44d724884b244e8fe4abb4"
    b"c67bbd2e652d5fc5942b55c24b7f642f65b9b6d37110a955c63eb4f26435be056effbd777f14db8d"
    b"3d8073f7583b24656edb19911e1307101443a50717c32dbb80b6212e6f0ee43f629b1e718a958a5c"
    b"fdcd99762f5bff821ac49b0e77c9d1426f8bb31142df030549330dde5cc92fa20d09744ceac6ae02"
    b"fb354e9b930173e08488375f7c795b3b934c72b58a3353332d5129d56151b57a793d99868885ebd4"
    b"aac11ca03e09f5b6bd9dda5322a0ab81e468839ea373ecd2b5ac4ffc99740581b35add07f83ff18e"
    b"c2111555ead17783294b2330ad874bd966c1d60b44e5f379650910a8a05eb92cb7550191c13251f5"
    b"0a11afa7510282010100c5a4aa380f6bdd4b4524deb44425aa7ef61039a46ad0d09e2ca2cd7fb757"
    b"ff325f81eaf3a2e790afb3ffb0d71f3ffa52db1a24d3149839f03d1acfe33ef721fe310895986c5a"
    b"fe88ceb82318ed540456b8aa7e07dc7b982345c4f040b1544bd2ee1e4cb0315bd8db3794ea93d705"
    b"f41cc1c06badf72de36d2b4a4399846d6c851260e5044e9495be8225307edb97071bdea08c99ccfe"
    b"54219f6a785db47864e03cf2851abcb62941d3efeea7cdf136d9e23845cf9ea0323b156c686c6d30"
    b"1cbb5a8c7f1db23a998bf549874b2c13685b20d200d2d91be92c40480a0cca18c28f654dd644c60d"
    b"e8e03824c0ff83e7cbfc44b2aa16ad537a09565ed4afbe63b8930282010100c01a5e6108420c3d2e"
    b"ccd0b559e08680f47b3e7271ee4ea9bf4740cc5c418a53225778eddb716447b02d234909f8291581"
    b"a45be0591952bacda55e774338962502c1d73f2d5383259aaa69f2603fde216ca9557d8b4e629888"
    b"c697fec1aaf9f99ebd223c06399cc13cd21bd01e3660acc148ba841e5c89b3f8f04efac07f8072a5"
    b"bacb4f5cfece528496bb35e906361efdb89a17fe4999f47508d5e48914ac651172ddc994993b4672"
    b"7ec62810d6c204af4b5fd52ba4f8cb3c8720fbd469b219868e28294e60276bc2483e78d96a0edf29"
    b"e237fe6f1660705d5cd3590c476e37c5d367b19bfb0a1c29ef296dfd3e9fabf5b37e1fb7357a3032"
    b"c8a641b467d7090282010100bc6d55bf66ac6e69017dba38e0b38c4dc8a8055c845d9a5702b51ff8"
    b"4042cbd1298f0201cf70b7d75b634d247aed92e9056c72692f3c46188d190fd35647648824154c11"
    b"ea54025149cbf1e224f9b1bd4007836a594117f5a0e1b62fe72037bddc38d4e231dc9fedb79ae8dd"
    b"93e5602b3e6905fff02536aaf0d7b78517e4fece0b8c872ac9040d93781e9e92832604a80462ca49"
    b"234fe1c3c0695061fdd9be4aaeb08447ce5c590f2250a01629586bf3e421c424c1d576ae2fa99010"
    b"b7346460165ed61de8bac782d0928e4313bd59037051e6691e85e692c2a22bbaafbe555742bca7a8"
    b"1fae4933e332df317b7f3551c7e91211d6a33c38c4b85a4b46d769b3028201003884497a00a4f5d6"
    b"d63af9b830fe06744ff926512345ba2ce49280f4debb858799d5e4450e4798fa2251d54cbabb20d3"
    b"2bf5fff5cc20d01f173b6cc467a9713ae849c11adc29f2ae90874c6e3b74eed42494d90afb7e0f31"
    b"d323a23a181e4636f345af99bb371df01805b49b11186c6ec6daafcd08e5aeb99d268e05e5b65d42"
    b"dd914c194841cacfaa24726594edf7e43c3f204ea8c85c9bf806a66efb097302b514773dc41324c6"
    b"400f1e1b5180ed49d58cb6600fdc143a2ecf8e9ba84d8451502de890e6771181f981a9a782475aa2"
    b"bb3ecbbc76503e0530e28b676a5e6585d114b63021b4c4afae82a74cadb1cbe61a7e393ff975a942"
    b"1edebb531f51618902820100214d9f1efa774b9d4e0a996442c2744560c84b133045b1af9241d60f"
    b"c2f82043ac169dc9496ebb5f26b5cb8a6636c57d44e06843bf1f082be42fe5933a7ab7a6878dccf3"
    b"58606a9fd6984ea525fe34f9e86f7bae33e707be0dec8fbef2deed253c822f6b812e7bd8c64bc302"
    b"5c9a9e58811d30981a329f7b130148b0eb2ac62cec516942f7530963edab832bd0bacf344b183b9d"
    b"ba9d54535dceff640f94d79599edf8dd0c32029950ede63f2f579b0d3c9a13c04df73fec03c4bcbe"
    b"ff7ecf69ba082445673a263685475b91390963e2d42705ba89ff107e96bbb7a887daa016f282f1e6"
    b"bdd7b9bb14579166f8c13be876cdef07e13c6ef08ff49d4207c7c7ff"
)


# Server
def instance0():
    multitest.globals(IP=multitest.get_network_ip())
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(socket.getaddrinfo("0.0.0.0", PORT)[0][-1])
    s.listen(1)
    multitest.next()
    s2, _ = s.accept()
    server_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server_ctx.load_cert_chain(cert, keyfile=key)
    s2 = server_ctx.wrap_socket(s2, server_side=True)
    print(s2.read(16))
    s2.write(b"server to client")
    s2.close()
    s.close()


# Client
def instance1():
    multitest.next()
    s = socket.socket()
    s.connect(socket.getaddrinfo(IP, PORT)[0][-1])
    client_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client_ctx.verify_mode = ssl.CERT_REQUIRED
    client_ctx.load_verify_locations(cadata=cert)
    s = client_ctx.wrap_socket(s, server_hostname="micropython.local")
    s.write(b"client to server")
    print(s.read(16))
    s.close()