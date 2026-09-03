/****************************************************************************
 Copyright (c) 2012      greathqy
 Copyright (c) 2012      cocos2d-x.org
 Copyright (c) 2013-2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2019-present Axmol Engine contributors (see AUTHORS.md).

 https://axmol.dev/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "axmol/network/HttpClient-wasm.h"
#include <queue>
#include <cstdint>
#include "axmol/base/Director.h"
#include "axmol/platform/FileUtils.h"
#include "yasio/tlx/string_view.hpp"

#include <emscripten/fetch.h>
#include <emscripten.h>

namespace ax
{

namespace network
{

struct fetchUserData
{
    bool isAlone;
    HttpResponse* response;
};

static HttpClient* _httpClient = nullptr;  // pointer to singleton

// HttpClient implementation
HttpClient* HttpClient::getInstance()
{
    if (_httpClient == nullptr)
    {
        _httpClient = new (std::nothrow) HttpClient();
    }

    return _httpClient;
}

HttpClient::HttpClient()
    : _timeoutForConnect(30)
    , _timeoutForRead(60)
    , _threadCount(0)
    , _cookie(nullptr)
    , _clearRequestPredicate(nullptr)
    , _clearResponsePredicate(nullptr)
{
    AXLOGD("In the constructor of HttpClient!");
    increaseThreadCount();
}

HttpClient::~HttpClient()
{
    AXLOGD("HttpClient destructor");
}

void HttpClient::destroyInstance()
{
    if (nullptr == _httpClient)
    {
        AXLOGD("HttpClient singleton is nullptr");
    }

    auto thiz   = _httpClient;
    _httpClient = nullptr;

    auto& requestQueue = thiz->_requestQueue;
    for (auto it = requestQueue.begin(); it != requestQueue.end();)
        it = requestQueue.erase(it);

    thiz->decreaseThreadCountAndMayDeleteThis();
}

void HttpClient::enableCookies(const char* cookieFile)
{
    if (cookieFile)
    {
        _cookieFilename = std::string(cookieFile);
    }
    else
    {
        _cookieFilename = (FileUtils::getInstance()->getWritablePath() + "cookieFile.txt");
    }
}

void HttpClient::setSSLVerification(std::string_view caFile)
{
    AXLOGD("HttpClient::setSSLVerification not required on Emscripten");
    // _sslCaFilename = caFile;
}

// Add a get task to queue
void HttpClient::send(HttpRequest* request)
{
    if (!request)
        return;

    // request ref +1 by response or _requestQueue
    if (_threadCount <= 1)
    {
        increaseThreadCount();
        HttpResponse* response = new (std::nothrow) HttpResponse(request);
        processResponse(response, false);
    }
    else
    {
        _requestQueue.pushBack(request);
    }
}

void HttpClient::sendImmediate(HttpRequest* request)
{
    if (!request)
        return;

    HttpResponse* response = new (std::nothrow) HttpResponse(request);
    processResponse(response, true);
}

// Process Response
void HttpClient::processResponse(HttpResponse* response, bool isAlone)
{
    // copy cookie back to document.cookie in case it is changed
    std::string_view cookieFilename = HttpClient::getInstance()->getCookieFilename();
    if (!cookieFilename.empty())
    {
        EM_ASM_ARGS(
            {
                try
                {  // suppress cookie file not exist exception at first time
                    document.cookie = FS.readFile(UTF8ToString($0));
                }
                catch (e)
                {}
            },
            cookieFilename.data());
    }

    auto request = response->getHttpRequest();

    const char* method = "GET";
    switch (request->getRequestType())
    {
    case HttpRequest::Type::GET:    method = "GET";    break;
    case HttpRequest::Type::PATCH:  method = "PATCH";  break;
    case HttpRequest::Type::POST:   method = "POST";   break;
    case HttpRequest::Type::PUT:    method = "PUT";    break;
    case HttpRequest::Type::DELETE: method = "DELETE"; break;
    default:
        AXASSERT(false, "HttpClient: unknown request type, only GET, PATCH, POST, PUT or DELETE is supported");
        break;
    }

    auto userData      = new (std::nothrow) fetchUserData();
    userData->isAlone  = isAlone;
    userData->response = response;

    // "key:value\n..." (skip user-agent, which the browser forbids setting)
    std::string headerBlob;
    for (std::string_view header : request->getHeaders())
    {
        if (tlx::ic::starts_with(header, "user-agent"))
            continue;
        if (header.find(":") == std::string_view::npos)
            continue;
        headerBlob.append(header);
        headerBlob.push_back('\n');
    }

    const char* body = request->getRequestDataSize() ? request->getRequestData() : "";
    int bodyLen      = static_cast<int>(request->getRequestDataSize());
    int timeoutMs    = (getTimeoutForConnect() + getTimeoutForRead()) * 1000;
    std::string url{request->getUrl()};

    // Direct async XHR. axmol's emscripten_fetch path routes through a Fetch worker under
    // -sFETCH+pthreads that can stall and never fire its callback, hanging every request.
    // clang-format off
    EM_ASM({
        var userData = $0;
        var timeout = $6;
        var xhr = new XMLHttpRequest();
        xhr.open(UTF8ToString($1), UTF8ToString($2), true);
        xhr.responseType = 'arraybuffer';
        if (timeout > 0) xhr.timeout = timeout;
        UTF8ToString($3).split('\n').forEach(function(h) {
            var i = h.indexOf(':');
            if (i < 0) return;
            try { xhr.setRequestHeader(h.substring(0, i).trim(), h.substring(i + 1).trim()); } catch (e) {}
        });
        function done(status) {
            var ptr = 0;
            var len = 0;
            if (xhr.response) {
                var bytes = new Uint8Array(xhr.response);
                len = bytes.length;
                if (len > 0) { ptr = _malloc(len); HEAPU8.set(bytes, ptr); }
            }
            _axmol_http_oncomplete(userData, status, ptr, len);
            if (ptr) _free(ptr);
        }
        xhr.onload    = function() { done(xhr.status); };
        xhr.onerror   = function() { done(0); };
        xhr.ontimeout = function() { done(0); };
        try {
            xhr.send($5 > 0 ? new Uint8Array(HEAPU8.subarray($4, $4 + $5)) : null);
        } catch (e) { done(0); }
    },
    userData, method, url.c_str(), headerBlob.c_str(), body, bodyLen, timeoutMs);
    // clang-format on
}

extern "C" EMSCRIPTEN_KEEPALIVE void axmol_http_oncomplete(int userData, int status, int data, int len)
{
    HttpClient::onRequestComplete(reinterpret_cast<void*>(static_cast<intptr_t>(userData)), status,
                                  reinterpret_cast<const char*>(static_cast<intptr_t>(data)), len);
}

void HttpClient::onRequestComplete(void* userDataPtr, int status, const char* data, int len)
{
    fetchUserData* userData = reinterpret_cast<fetchUserData*>(userDataPtr);
    tlx::retain_ptr<HttpResponse> response{userData->response, tlx::adopt_object};
    HttpRequest* request = response->getHttpRequest();

    response->setResponseCode(status);
    if (data && len > 0)
        response->getResponseData()->assign(data, data + len);

    // write cookie back
    auto cookieFilename = HttpClient::getInstance()->getCookieFilename();
    if (!cookieFilename.empty())
    {
        EM_ASM_ARGS({ FS.writeFile(UTF8ToString($0), document.cookie); }, cookieFilename.data());
    }

    const auto isAlone = userData->isAlone;
    delete userData;

    if (_httpClient)
    {
        // call back
        const auto& callback = request->getCompleteCallback();
        if (callback)
            callback(_httpClient, response);
        response.reset();

        // call next request
        if (!isAlone)
        {
            auto& requestQueue = _httpClient->_requestQueue;
            if (!requestQueue.empty())
            {
                HttpRequest* nextRequest = requestQueue.at(0);
                HttpResponse* nextResp   = new (std::nothrow) HttpResponse(nextRequest);
                requestQueue.erase(0);
                _httpClient->processResponse(nextResp, false);
            }
            else
            {
                _httpClient->decreaseThreadCountAndMayDeleteThis();
            }
        }
    }
}

void HttpClient::clearResponseAndRequestQueue()
{
    for (auto it = _requestQueue.begin(); it != _requestQueue.end();)
    {
        if (!_clearRequestPredicate || _clearRequestPredicate((*it)))
        {
            it = _requestQueue.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void HttpClient::increaseThreadCount()
{
    ++_threadCount;
}

void HttpClient::decreaseThreadCountAndMayDeleteThis()
{
    --_threadCount;
    if (0 == _threadCount)
    {
        delete this;
    }
}

void HttpClient::setTimeoutForConnect(int value)
{
    _timeoutForConnect = value;
}

int HttpClient::getTimeoutForConnect()
{
    return _timeoutForConnect;
}

void HttpClient::setTimeoutForRead(int value)
{
    _timeoutForRead = value;
}

int HttpClient::getTimeoutForRead()
{
    return _timeoutForRead;
}

std::string_view HttpClient::getCookieFilename()
{
    return _cookieFilename;
}

std::string_view HttpClient::getSSLVerification()
{
    return _sslCaFilename;
}

}  // namespace network

}  // namespace ax
