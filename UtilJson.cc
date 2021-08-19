/**
 * @file UtilJson.cc
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2021 Zachary Parker
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "UtilJson.hh"

namespace draft::util {

void to_json(nlohmann::json &j, const FileInfo::Status &status)
{
    j = {
        {"mode", status.mode},
        {"uid", status.uid},
        {"gid", status.gid},
        {"dev", status.dev},
        {"blksize", status.blkSize},
        {"blocks", status.blkCount},
        {"size", status.size}
    };
}

void to_json(nlohmann::json &j, const FileInfo &info)
{
    j = {
        {"path", info.path},
        {"status", info.status},
        {"id", info.id}
    };

    if (!info.targetSuffix.empty())
        j["target_suffix"] = info.targetSuffix;
}

void from_json(const nlohmann::json &j, FileInfo::Status &status)
{
    j.at("mode").get_to(status.mode);
    j.at("uid").get_to(status.uid);
    j.at("gid").get_to(status.gid);
    j.at("dev").get_to(status.dev);
    j.at("blksize").get_to(status.blkSize);
    j.at("blocks").get_to(status.blkCount);
    j.at("size").get_to(status.size);
}

void from_json(const nlohmann::json &j, FileInfo &info)
{
    j.at("path").get_to(info.path);
    if (j.contains("target_suffix"))
        j["target_suffix"].get_to(info.targetSuffix);
    j.at("status").get_to(info.status);
    j.at("id").get_to(info.id);
}

Buffer generateTransferRequestMsg(std::vector<FileInfo> info)
{
    auto j = nlohmann::json{ };
    j["type"] = 0;
    j["client"] = 0;
    j["info"] = info;

    auto buf = Buffer{ };
    buf.resize(sizeof(wire::ChunkHeader));

    nlohmann::json::to_cbor(j, buf);

    auto header = reinterpret_cast<wire::ChunkHeader *>(buf.data());
    header->magic = wire::ChunkHeader::Magic;
    header->payloadLength = buf.size() - sizeof(wire::ChunkHeader);

    return buf;
}

TransferRequest deserializeTransferRequest(const Buffer &buf)
{
    if (buf.size() < sizeof(wire::ChunkHeader))
    {
        throw std::invalid_argument(fmt::format("request buffer is too short to contain a valid request: {}/{}"
            , buf.size()
            , sizeof(wire::ChunkHeader)));
    }

    const auto j = nlohmann::json::from_cbor(buf);
    spdlog::info("req: {}", j.dump(4));

    auto req = TransferRequest{ };
    j.at("info").get_to(req.config.fileInfo);

    return req;
}

}
