/*
===========================================================================
Oz Arena: the online identity (ENGINE_PLAN.md P7). GPL-2.0.

An RSA-2048 key in PEM, as Godot's CryptoKey.save writes it (both use mbedTLS), so the Godot build's
user://online/identity.key carries over: same player id, friends, level and rating. The public key
the hub sees is its SubjectPublicKeyInfo DER in base64 (Godot's save_to_string(true) without the PEM
lines), and the signature is RSASSA-PKCS1-v1_5 over SHA-256, as Godot's Crypto.sign makes it.
===========================================================================
*/
#include "oz_online.h"

#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

mbedtls_pk_context		key;
mbedtls_entropy_context	entropy;
mbedtls_ctr_drbg_context drbg;
bool					ready = false;
std::string				publicKey, playerId;

bool Seed() {
	static bool seeded = false;
	if ( seeded ) {
		return true;
	}
	mbedtls_entropy_init( &entropy );
	mbedtls_ctr_drbg_init( &drbg );
	const char *who = "oz-arena identity";
	if ( mbedtls_ctr_drbg_seed( &drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)who, strlen( who ) ) != 0 ) {
		return false;
	}
	seeded = true;
	return true;
}

std::string Base64( const unsigned char *data, size_t len ) {
	size_t n = 0;
	mbedtls_base64_encode( nullptr, 0, &n, data, len );
	std::string out( n, '\0' );
	if ( mbedtls_base64_encode( (unsigned char *)out.data(), n, &n, data, len ) != 0 ) {
		return {};
	}
	out.resize( n );
	return out;
}

// a path's file, by its wide name on Windows (any characters), its bytes elsewhere
FILE *OpenPath( const std::filesystem::path &p, bool write ) {
#ifdef _WIN32
	return _wfopen( p.c_str(), write ? L"wb" : L"rb" );
#else
	return fopen( p.c_str(), write ? "wb" : "rb" );
#endif
}

bool ReadFile( const std::filesystem::path &p, std::string &out ) {
	FILE *f = OpenPath( p, false );
	if ( !f ) {
		return false;
	}
	char buf[4096];
	size_t n;
	out.clear();
	while ( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 ) {
		out.append( buf, n );
	}
	fclose( f );
	return true;
}

// The key, written beside itself and renamed over: written in place, a crash or a full disk mid-write left half a
// key, and the player was someone else from then on. On Linux it's the player's alone to read (0600).
bool WriteFile( const std::filesystem::path &p, const std::string &data ) {
	std::error_code ec;
	std::filesystem::create_directories( p.parent_path(), ec );
	std::filesystem::path tmp = p;
	tmp += ".tmp";
#ifdef _WIN32
	FILE *f = OpenPath( tmp, true );
#else
	const int fd = open( tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600 );
	FILE *f = nullptr;
	if ( fd >= 0 ) {
		fchmod( fd, 0600 );		// a file left from before keeps its old mode otherwise
		f = fdopen( fd, "wb" );
		if ( !f ) {
			close( fd );
		}
	}
#endif
	if ( !f ) {
		return false;
	}
	bool ok = fwrite( data.data(), 1, data.size(), f ) == data.size();
	ok = fflush( f ) == 0 && ok;
	ok = fclose( f ) == 0 && ok;
	if ( ok ) {
		std::filesystem::rename( tmp, p, ec );	// over the old file, on Windows too
		ok = !ec;
	}
	if ( !ok ) {
		std::filesystem::remove( tmp, ec );
	}
	return ok;
}

bool Parse( const std::string &pem ) {
	// mbedtls_pk_parse_key wants the PEM's terminating NUL counted
	return mbedtls_pk_parse_key( &key, (const unsigned char *)pem.c_str(), pem.size() + 1, nullptr, 0,
		mbedtls_ctr_drbg_random, &drbg ) == 0 && mbedtls_pk_get_type( &key ) == MBEDTLS_PK_RSA;
}

bool Generate( std::string &pem ) {
	if ( mbedtls_pk_setup( &key, mbedtls_pk_info_from_type( MBEDTLS_PK_RSA ) ) != 0 ||
		 mbedtls_rsa_gen_key( mbedtls_pk_rsa( key ), mbedtls_ctr_drbg_random, &drbg, 2048, 65537 ) != 0 ) {
		return false;
	}
	std::vector<unsigned char> buf( 8192 );
	if ( mbedtls_pk_write_key_pem( &key, buf.data(), buf.size() ) != 0 ) {
		return false;
	}
	pem = (const char *)buf.data();
	return true;
}

}	// namespace

int OzIdentity_Load( const char *path, const char *importFrom, char *error, int errorSize ) {
	auto fail = [&]( const char *why ) {
		snprintf( error, errorSize, "%s", why );
		return 0;
	};
	if ( error && errorSize > 0 ) {
		error[0] = '\0';
	}
	if ( ready ) {
		return 1;
	}
	if ( !Seed() ) {
		return fail( "No random numbers for a key." );
	}
	mbedtls_pk_init( &key );
	const std::filesystem::path mine = std::filesystem::path( std::string( path ) );
	std::string pem;
	bool have = ReadFile( mine, pem ), spoiled = false;
	if ( have && !Parse( pem ) ) {
		// A key file that isn't a key (cut short by a crash, a full disk): it used to end online play on this PC for
		// good. It's kept beside as identity.key.bad, and the player gets a new identity, as on a first run.
		std::error_code ec;
		std::filesystem::path bad = mine;
		bad += ".bad";
		std::filesystem::rename( mine, bad, ec );
		if ( ec ) {
			return fail( "The online identity file can't be read." );
		}
		mbedtls_pk_free( &key );
		mbedtls_pk_init( &key );
		have = false;
		spoiled = true;
	}
	if ( have ) {
#ifndef _WIN32
		std::error_code ec;
		std::filesystem::permissions( mine, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
			std::filesystem::perm_options::replace, ec );	// a key from before it was made 0600
#endif
	} else if ( importFrom && importFrom[0] && ReadFile( std::filesystem::path( std::string( importFrom ) ), pem ) && Parse( pem ) ) {
		WriteFile( mine, pem );		// the Godot build's player, kept as is
		if ( spoiled ) {
			snprintf( error, errorSize, "The online identity file couldn't be read. It is kept as identity.key.bad, and the earlier build's identity is used." );
		}
	} else {
		mbedtls_pk_free( &key );
		mbedtls_pk_init( &key );
		if ( !Generate( pem ) || !WriteFile( mine, pem ) ) {
			return fail( "Couldn't create this PC's online identity." );
		}
		if ( spoiled ) {
			snprintf( error, errorSize, "The online identity file couldn't be read. It is kept as identity.key.bad, and this PC has a new identity: friends have to be added again." );
		}
	}

	unsigned char der[2048];
	int len = mbedtls_pk_write_pubkey_der( &key, der, sizeof( der ) );	// written at the buffer's end
	if ( len <= 0 ) {
		return fail( "The online identity has no public key." );
	}
	const unsigned char *start = der + sizeof( der ) - len;
	publicKey = Base64( start, len );
	unsigned char hash[32];
	mbedtls_sha256( start, len, hash, 0 );
	char hex[41];
	for ( int i = 0; i < 10; i++ ) {
		snprintf( hex + i * 2, 3, "%02x", hash[i] );
	}
	playerId = hex;
	ready = true;
	return 1;
}

const char *OzIdentity_PublicKey( void ) {
	return ready ? publicKey.c_str() : "";
}

const char *OzIdentity_PlayerId( void ) {
	return ready ? playerId.c_str() : "";
}

int OzIdentity_Sign( const char *text, char *b64, int size ) {
	if ( !ready ) {
		return 0;
	}
	unsigned char hash[32], sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
	size_t sigLen = 0;
	mbedtls_sha256( (const unsigned char *)text, strlen( text ), hash, 0 );
	if ( mbedtls_pk_sign( &key, MBEDTLS_MD_SHA256, hash, sizeof( hash ), sig, sizeof( sig ), &sigLen,
			mbedtls_ctr_drbg_random, &drbg ) != 0 ) {
		return 0;
	}
	std::string out = Base64( sig, sigLen );
	if ( out.empty() || (int)out.size() >= size ) {
		return 0;
	}
	memcpy( b64, out.c_str(), out.size() + 1 );
	return 1;
}
