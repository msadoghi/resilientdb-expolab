/*
 * Copyright (c) 2019-2022 ExpoLab, UC Davis
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include <fcntl.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/mime.h>
#include <pistache/net.h>
#include <pistache/router.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config/resdb_config_utils.h"
#include "kv_client/resdb_kv_client.h"
#include "proto/signature_info.pb.h"
#include "sdk_client/sdk_transaction.h"

namespace resdb {

class PistacheService {
 public:
  PistacheService(ResDBConfig config, uint16_t portNum = 8000,
                  unsigned int numThreads = std::thread::hardware_concurrency())
      : config_(config),
        portNum_(portNum),
        numThreads_(numThreads),
        address_("localhost", portNum),
        endpoint_(std::make_shared<Pistache::Http::Endpoint>(address_)) {}

  void run();

 private:
  void configureRoutes();

  using Request = Pistache::Rest::Request;
  using Response = Pistache::Http::ResponseWriter;
  void commit(const Request& request, Response response);
  void getAssets(const Request& request, Response response);

  ResDBConfig config_;
  uint16_t portNum_;
  unsigned int numThreads_;
  Pistache::Address address_;
  std::shared_ptr<Pistache::Http::Endpoint> endpoint_;
  Pistache::Rest::Router router_;
};

}  // namespace resdb