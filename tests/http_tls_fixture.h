#pragma once
#include "http_fixture.h"
#include <filesystem>
#include <ctime>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>

// Ephemeral PKI and loopback CRL endpoint. No OS trust-store changes.
// The production HTTP client keeps its normal certificate and revocation policy.
class HttpTlsFixture {
    template<class T,void(*Free)(T*)> using Ptr=std::unique_ptr<T,decltype(Free)>;
    std::filesystem::path directory;
    std::string crl_der;
    std::unique_ptr<HttpFixture> crl_server;
    static void check(bool ok) {if(!ok) throw std::runtime_error("TLS fixture PKI setup failed");}
    static Ptr<EVP_PKEY,EVP_PKEY_free> key() {
        Ptr<EVP_PKEY_CTX,EVP_PKEY_CTX_free> context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA,nullptr),EVP_PKEY_CTX_free);
        check(context!=nullptr);
        check(EVP_PKEY_keygen_init(context.get())>0&&EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(),2048)>0);
        EVP_PKEY* value=nullptr;check(EVP_PKEY_keygen(context.get(),&value)>0);
        return {value,EVP_PKEY_free};
    }
    static void extension(X509* cert,X509* issuer,int nid,const std::string& value) {
        X509V3_CTX context{};X509V3_set_ctx(&context,issuer,cert,nullptr,nullptr,0);
        Ptr<X509_EXTENSION,X509_EXTENSION_free> item(
            X509V3_EXT_conf_nid(nullptr,&context,nid,value.c_str()),X509_EXTENSION_free);
        check(item!=nullptr&&X509_add_ext(cert,item.get(),-1)==1);
    }
    static Ptr<X509,X509_free> certificate(EVP_PKEY* public_key,X509* issuer,
                                          EVP_PKEY* signing_key,bool ca,const std::string& crl_url) {
        Ptr<X509,X509_free> cert(X509_new(),X509_free);check(cert!=nullptr);
        check(X509_set_version(cert.get(),2)==1&&ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),ca?1:2)==1);
        check(X509_gmtime_adj(X509_getm_notBefore(cert.get()),-300)!=nullptr);
        check(X509_gmtime_adj(X509_getm_notAfter(cert.get()),86400)!=nullptr);
        check(X509_set_pubkey(cert.get(),public_key)==1);
        X509_NAME* name=X509_get_subject_name(cert.get());
        const char* cn=ca?"Axon ephemeral test CA":"Axon loopback TLS fixture";
        check(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,
              reinterpret_cast<const unsigned char*>(cn),-1,-1,0)==1);
        check(X509_set_issuer_name(cert.get(),issuer?X509_get_subject_name(issuer):name)==1);
        extension(cert.get(),issuer?issuer:cert.get(),NID_basic_constraints,ca?"critical,CA:TRUE,pathlen:0":"critical,CA:FALSE");
        extension(cert.get(),issuer?issuer:cert.get(),NID_key_usage,ca?"critical,keyCertSign,cRLSign":"critical,digitalSignature,keyEncipherment");
        extension(cert.get(),issuer?issuer:cert.get(),NID_subject_key_identifier,"hash");
        if(!ca) {
            extension(cert.get(),issuer,NID_ext_key_usage,"serverAuth");
            extension(cert.get(),issuer,NID_subject_alt_name,"IP:127.0.0.1");
            extension(cert.get(),issuer,NID_crl_distribution_points,"URI:"+crl_url);
            extension(cert.get(),issuer,NID_authority_key_identifier,"keyid:always");
        }
        check(X509_sign(cert.get(),signing_key,EVP_sha256())>0);return cert;
    }
    void cleanup() noexcept {
        crl_server.reset();std::error_code ignored;
        if(!directory.empty()) std::filesystem::remove_all(directory,ignored);
    }
public:
    std::string ca_file,certificate_file,key_file;
    explicit HttpTlsFixture(bool revoked=false) {
        try {
            unsigned char nonce[16];check(RAND_bytes(nonce,sizeof(nonce))==1);
            std::string suffix;const char* hex="0123456789abcdef";
            for(auto byte:nonce) {suffix+=hex[byte>>4];suffix+=hex[byte&15];}
            const auto candidate=std::filesystem::temp_directory_path()/("axon-http-tls-"+suffix);
            check(std::filesystem::create_directory(candidate));directory=candidate;
            ca_file=(directory/"ca.pem").string();certificate_file=(directory/"server.pem").string();key_file=(directory/"server.key").string();
            crl_server=std::make_unique<HttpFixture>([this](const std::string&) {
                return HttpFixture::response(crl_der,200,"Content-Type: application/pkix-crl\r\n");
            });
            auto ca_key=key();auto server_key=key();
            auto ca=certificate(ca_key.get(),nullptr,ca_key.get(),true,"");
            auto server=certificate(server_key.get(),ca.get(),ca_key.get(),false,crl_server->url("/"+suffix+".crl"));
            Ptr<X509_CRL,X509_CRL_free> crl(X509_CRL_new(),X509_CRL_free);check(crl!=nullptr);
            check(X509_CRL_set_version(crl.get(),1)==1&&X509_CRL_set_issuer_name(crl.get(),X509_get_subject_name(ca.get()))==1);
            Ptr<ASN1_TIME,ASN1_TIME_free> before(ASN1_TIME_adj(nullptr,std::time(nullptr),0,-300),ASN1_TIME_free);
            Ptr<ASN1_TIME,ASN1_TIME_free> after(ASN1_TIME_adj(nullptr,std::time(nullptr),1,0),ASN1_TIME_free);
            check(before!=nullptr&&after!=nullptr);
            check(X509_CRL_set1_lastUpdate(crl.get(),before.get())==1&&X509_CRL_set1_nextUpdate(crl.get(),after.get())==1);
            if(revoked) {
                Ptr<X509_REVOKED,X509_REVOKED_free> entry(X509_REVOKED_new(),X509_REVOKED_free);check(entry!=nullptr);
                check(X509_REVOKED_set_serialNumber(entry.get(),X509_get_serialNumber(server.get()))==1);
                check(X509_REVOKED_set_revocationDate(entry.get(),before.get())==1);
                check(X509_CRL_add0_revoked(crl.get(),entry.get())==1);entry.release();
            }
            check(X509_CRL_sort(crl.get())==1&&X509_CRL_sign(crl.get(),ca_key.get(),EVP_sha256())>0);
            check(X509_CRL_verify(crl.get(),ca_key.get())==1);
            const int size=i2d_X509_CRL(crl.get(),nullptr);check(size>0);
            crl_der.resize(static_cast<size_t>(size));
            auto* bytes=reinterpret_cast<unsigned char*>(&crl_der[0]);check(i2d_X509_CRL(crl.get(),&bytes)==size);
            // Validate both the clean and revoked fixture chains on every platform.
            Ptr<X509_STORE,X509_STORE_free> store(X509_STORE_new(),X509_STORE_free);
            Ptr<X509_STORE_CTX,X509_STORE_CTX_free> context(X509_STORE_CTX_new(),X509_STORE_CTX_free);
            check(store!=nullptr&&context!=nullptr);
            check(X509_STORE_add_cert(store.get(),ca.get())==1&&X509_STORE_add_crl(store.get(),crl.get())==1);
            check(X509_STORE_set_flags(store.get(),X509_V_FLAG_CRL_CHECK)==1);
            check(X509_STORE_CTX_init(context.get(),store.get(),server.get(),nullptr)==1);
            const int verified=X509_verify_cert(context.get());
            check(revoked?(verified==0&&X509_STORE_CTX_get_error(context.get())==X509_V_ERR_CERT_REVOKED):verified==1);
            auto pem=[&](const std::string& path,const std::function<int(BIO*)>& write) {
                Ptr<BIO,BIO_free_all> file(BIO_new_file(path.c_str(),"wb"),BIO_free_all);
                check(file!=nullptr&&write(file.get())==1);
            };
            pem(ca_file,[&](BIO* file){return PEM_write_bio_X509(file,ca.get());});
            pem(certificate_file,[&](BIO* file){return PEM_write_bio_X509(file,server.get());});
            pem(key_file,[&](BIO* file){return PEM_write_bio_PrivateKey(file,server_key.get(),nullptr,nullptr,0,nullptr,nullptr);});
        } catch(...) {cleanup();throw;}
    }
    ~HttpTlsFixture() {cleanup();}
    HttpTlsFixture(const HttpTlsFixture&)=delete;
    HttpTlsFixture& operator=(const HttpTlsFixture&)=delete;
};
