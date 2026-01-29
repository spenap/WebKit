/*
 * Copyright (C) 2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "config.h"
#import "_WKJSHandleInternal.h"

#import "FrameInfoData.h"
#import "WKContentWorldInternal.h"
#import "WKFrameInfoInternal.h"
#import "WKRemoteObjectCoder.h"
#import "WebFrameProxy.h"
#import "WebPageProxy.h"
#import <WebCore/WebCoreObjCExtras.h>
#import <WebCore/WebKitJSHandle.h>
#import <wtf/BlockPtr.h>

@implementation _WKJSHandle

- (void)dealloc
{
    if (WebCoreObjCScheduleDeallocateOnMainRunLoop(_WKJSHandle.class, self))
        return;
    SUPPRESS_UNRETAINED_ARG _ref->API::JSHandle::~JSHandle();
    [super dealloc];
}

- (WKFrameInfo *)frame
{
    return wrapper(API::FrameInfo::create(WebKit::FrameInfoData { _ref->info().frameInfo })).autorelease();
}

- (WKContentWorld *)world
{
    return wrapper(API::ContentWorld::worldForIdentifier(_ref->info().worldIdentifier));
}

- (void)windowFrameInfo:(void (^)(WKFrameInfo *))completionHandler
{
    RefPtr webFrame = WebKit::WebFrameProxy::webFrame(_ref->info().windowProxyFrameIdentifier);
    if (!webFrame)
        return completionHandler(nil);
    webFrame->getFrameInfo([completionHandler = makeBlockPtr(completionHandler)] (auto&& data) mutable {
        if (!data)
            return completionHandler(nil);
        completionHandler(wrapper(API::FrameInfo::create(WTF::move(*data))).get());
    });
}

- (BOOL)isEqual:(id)object
{
    if (self == object)
        return YES;

    if (![object isKindOfClass:self.class])
        return NO;

    return _ref->info() == ((_WKJSHandle *)object)->_ref->info();
}

- (NSUInteger)hash
{
    return _ref->info().identifier.object().toUInt64();
}

- (id)copyWithZone:(NSZone *)zone
{
    return [self retain];
}

- (API::Object&)_apiObject
{
    return *_ref;
}

// NSSecureCoding implementation.
// This is for injected bundle transition support only.
// Do not include this in a public API version of WKJSHandle.
+ (BOOL)supportsSecureCoding
{
#if PLATFORM(MAC)
    RELEASE_ASSERT(WTF::MacApplication::isSafari() || applicationBundleIdentifier() == "com.apple.WebKit.TestWebKitAPI"_s);
#else
    RELEASE_ASSERT(WTF::IOSApplication::isMobileSafari() || applicationBundleIdentifier() == "com.apple.WebKit.TestWebKitAPI"_s);
#endif
    return YES;
}

- (id)initWithCoder:(NSCoder *)decoder
{
    if (!(self = [super init]))
        return nil;

#if PLATFORM(MAC)
    RELEASE_ASSERT(WTF::MacApplication::isSafari() || applicationBundleIdentifier() == "com.apple.WebKit.TestWebKitAPI"_s);
#else
    RELEASE_ASSERT(WTF::IOSApplication::isMobileSafari() || applicationBundleIdentifier() == "com.apple.WebKit.TestWebKitAPI"_s);
#endif
    RELEASE_ASSERT(!isInAuxiliaryProcess());

    RetainPtr identifierObject = dynamic_objc_cast<NSNumber>([decoder decodeObjectOfClass:NSNumber.class forKey:@"identifierObject"]);
    if (!identifierObject) {
        [self release];
        return nil;
    }
    uint64_t rawIdentifierObject = identifierObject.get().unsignedLongLongValue;
    if (!WebCore::WebProcessJSHandleIdentifier::isValidIdentifier(rawIdentifierObject)) {
        [self release];
        return nil;
    }

    RetainPtr identifierProcessIdentifier = dynamic_objc_cast<NSNumber>([decoder decodeObjectOfClass:NSNumber.class forKey:@"identifierProcessIdentifier"]);
    if (!identifierProcessIdentifier) {
        [self release];
        return nil;
    }
    uint64_t rawIdentifierProcessIdentifier = identifierProcessIdentifier.get().unsignedLongLongValue;
    if (!WebCore::ProcessIdentifier::isValidIdentifier(rawIdentifierProcessIdentifier)) {
        [self release];
        return nil;
    }

    RetainPtr worldIdentifierObject = dynamic_objc_cast<NSNumber>([decoder decodeObjectOfClass:NSNumber.class forKey:@"worldIdentifierObject"]);
    if (!worldIdentifierObject) {
        [self release];
        return nil;
    }
    uint64_t rawWorldIdentifierObject = worldIdentifierObject.get().unsignedLongLongValue;
    if (!WebKit::NonProcessQualifiedContentWorldIdentifier::isValidIdentifier(rawWorldIdentifierObject)) {
        [self release];
        return nil;
    }

    RetainPtr worldIdentifierProcessIdentifier = dynamic_objc_cast<NSNumber>([decoder decodeObjectOfClass:NSNumber.class forKey:@"worldIdentifierProcessIdentifier"]);
    if (!worldIdentifierProcessIdentifier) {
        [self release];
        return nil;
    }
    uint64_t rawWorldIdentifierProcessIdentifier = worldIdentifierProcessIdentifier.get().unsignedLongLongValue;
    if (!WebCore::ProcessIdentifier::isValidIdentifier(rawWorldIdentifierProcessIdentifier)) {
        [self release];
        return nil;
    }

    uint64_t documentIDProcessIdentifier = [dynamic_objc_cast<NSNumber>([decoder decodeObjectOfClass:NSNumber.class forKey:@"documentIDProcessIdentifier"]) unsignedLongLongValue];
    uint64_t worldIdentifierHighBits = [dynamic_objc_cast<NSNumber>([decoder decodeObjectOfClass:NSNumber.class forKey:@"worldIdentifierHighBits"]) unsignedLongLongValue];
    uint64_t worldIdentifierLowBits = [dynamic_objc_cast<NSNumber>([decoder decodeObjectOfClass:NSNumber.class forKey:@"worldIdentifierLowBits"]) unsignedLongLongValue];

    Markable<WebCore::ScriptExecutionContextIdentifier> documentID;
    if (WebCore::ProcessIdentifier::isValidIdentifier(documentIDProcessIdentifier) && WTF::UUID::isValid(worldIdentifierHighBits, worldIdentifierLowBits))
        documentID = WebCore::ScriptExecutionContextIdentifier { WTF::UUID(worldIdentifierHighBits, worldIdentifierLowBits), WebCore::ProcessIdentifier(documentIDProcessIdentifier) };

    WebCore::JSHandleIdentifier identifier { WebCore::WebProcessJSHandleIdentifier(rawIdentifierObject), WebCore::ProcessIdentifier(rawIdentifierProcessIdentifier) };
    WebKit::ContentWorldIdentifier worldIdentifier { WebKit::NonProcessQualifiedContentWorldIdentifier(rawWorldIdentifierObject), WebCore::ProcessIdentifier(rawWorldIdentifierProcessIdentifier) };

    auto frameInfo = WebKit::legacyEmptyFrameInfo({ });
    frameInfo.documentID = documentID;

    WebKit::JSHandleInfo info {
        identifier,
        worldIdentifier,
        WTF::move(frameInfo),
        std::nullopt
    };

    API::Object::constructInWrapper<API::JSHandle>(self, WTF::move(info));

    return self;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
#if PLATFORM(MAC)
    RELEASE_ASSERT(WTF::MacApplication::isSafari() || applicationBundleIdentifier() == "com.apple.WebKit.TestWebKitAPI"_s);
#else
    RELEASE_ASSERT(WTF::IOSApplication::isMobileSafari() || applicationBundleIdentifier() == "com.apple.WebKit.TestWebKitAPI"_s);
#endif
    RELEASE_ASSERT(isInWebProcess());

    const auto& info = _ref->info();
    WebCore::WebKitJSHandle::jsHandleSentToAnotherProcess(info.identifier);

    [coder encodeObject:@(info.identifier.object().toUInt64()) forKey:@"identifierObject"];
    [coder encodeObject:@(info.identifier.processIdentifier().toUInt64()) forKey:@"identifierProcessIdentifier"];

    [coder encodeObject:@(info.worldIdentifier.object().toUInt64()) forKey:@"worldIdentifierObject"];
    [coder encodeObject:@(info.worldIdentifier.processIdentifier().toUInt64()) forKey:@"worldIdentifierProcessIdentifier"];

    if (info.frameInfo.documentID) {
        [coder encodeObject:@(info.frameInfo.documentID->processIdentifier().toUInt64()) forKey:@"documentIDProcessIdentifier"];
        [coder encodeObject:@(info.frameInfo.documentID->object().high()) forKey:@"worldIdentifierHighBits"];
        [coder encodeObject:@(info.frameInfo.documentID->object().low()) forKey:@"worldIdentifierLowBits"];
    }

    // Remaining information is currently not needed, and this is on its way to being removed so let's not add more here.
    // This also avoids encoding any complex types.
}

@end
