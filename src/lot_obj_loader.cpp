#include "lot_obj_loader.h"
#include "lot_math.h"

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace lot_obj {
namespace {

// OBJ 의 f 줄은 "위치/텍스처/법선" 인덱스 묶음이다.
// 셋의 조합이 같으면 같은 정점이므로, 이걸 키로 중복을 제거한다.
struct IndexKey {
    int position = 0;
    int texcoord = 0;
    int normal = 0;

    bool operator==(const IndexKey& other) const {
        return position == other.position
            && texcoord == other.texcoord
            && normal == other.normal;
    }
};

struct IndexKeyHash {
    size_t operator()(const IndexKey& key) const {
        // 서로 다른 자리로 섞어주기만 하면 된다 (정점 수가 많지 않다)
        size_t h = static_cast<size_t>(key.position) * 73856093u;
        h ^= static_cast<size_t>(key.texcoord) * 19349663u;
        h ^= static_cast<size_t>(key.normal) * 83492791u;
        return h;
    }
};

// 공백으로 나뉜 다음 토큰을 집는다. 없으면 빈 문자열.
std::string nextToken(const std::string& line, size_t& pos) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    const size_t start = pos;
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') ++pos;
    return line.substr(start, pos - start);
}

// OBJ 인덱스는 1부터 시작하고, 음수면 '끝에서부터'를 뜻한다.
// 여기서는 0 부터 시작하는 인덱스로 바꿔 돌려준다. 없는 항목은 -1.
int resolveIndex(const std::string& token, size_t count) {
    if (token.empty()) return -1;
    const long raw = std::strtol(token.c_str(), nullptr, 10);
    if (raw > 0) return static_cast<int>(raw - 1);
    if (raw < 0) return static_cast<int>(static_cast<long>(count) + raw);
    return -1;
}

// "12/34/56", "12//56", "12/34", "12" 를 모두 받아준다.
IndexKey parseCorner(const std::string& token, size_t positionCount,
                     size_t texcoordCount, size_t normalCount) {
    std::string parts[3];
    int slot = 0;
    for (char c : token) {
        if (c == '/') {
            if (++slot > 2) break;
        } else {
            parts[slot].push_back(c);
        }
    }

    IndexKey key;
    key.position = resolveIndex(parts[0], positionCount);
    key.texcoord = resolveIndex(parts[1], texcoordCount);
    key.normal = resolveIndex(parts[2], normalCount);
    return key;
}

}  // namespace

LoadResult parse(const std::string& text) {
    LoadResult result;

    std::vector<vec3> positions;
    std::vector<vec3> colors;
    std::vector<vec3> normals;

    // 정점 인덱스 조합 -> 우리 정점 배열에서의 위치
    std::unordered_map<IndexKey, uint32_t, IndexKeyHash> uniqueVertices;

    // 법선이 없는 파일을 위해, 면 법선을 채워 넣을 정점들을 기억해둔다
    std::vector<uint32_t> pendingFlatNormals;

    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();

        std::string line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        if (!line.empty() && line.back() == '\r') line.pop_back();  // CRLF
        if (line.empty() || line[0] == '#') continue;

        size_t pos = 0;
        const std::string tag = nextToken(line, pos);

        if (tag == "v") {
            const std::string xs = nextToken(line, pos);
            const std::string ys = nextToken(line, pos);
            const std::string zs = nextToken(line, pos);
            if (xs.empty() || ys.empty() || zs.empty()) {
                result.error = "malformed 'v' line: " + line;
                return result;
            }
            positions.push_back(vec3{std::strtof(xs.c_str(), nullptr),
                                     std::strtof(ys.c_str(), nullptr),
                                     std::strtof(zs.c_str(), nullptr)});

            // 표준은 아니지만 x y z 뒤에 r g b 를 붙이는 파일이 흔하다.
            const std::string rs = nextToken(line, pos);
            const std::string gs = nextToken(line, pos);
            const std::string bs = nextToken(line, pos);
            if (!rs.empty() && !gs.empty() && !bs.empty()) {
                colors.push_back(vec3{std::strtof(rs.c_str(), nullptr),
                                      std::strtof(gs.c_str(), nullptr),
                                      std::strtof(bs.c_str(), nullptr)});
            } else {
                colors.push_back(vec3{1.0f, 1.0f, 1.0f});
            }
        } else if (tag == "vn") {
            const std::string xs = nextToken(line, pos);
            const std::string ys = nextToken(line, pos);
            const std::string zs = nextToken(line, pos);
            normals.push_back(vec3{std::strtof(xs.c_str(), nullptr),
                                   std::strtof(ys.c_str(), nullptr),
                                   std::strtof(zs.c_str(), nullptr)});
        } else if (tag == "f") {
            // 면의 꼭짓점을 먼저 다 모은다 (사각형 이상일 수 있다)
            std::vector<IndexKey> corners;
            for (;;) {
                const std::string token = nextToken(line, pos);
                if (token.empty()) break;
                corners.push_back(parseCorner(token, positions.size(),
                                              normals.size(), normals.size()));
            }
            if (corners.size() < 3) continue;  // 점이나 선은 건너뛴다

            // 꼭짓점 하나를 우리 정점 배열에 넣고 인덱스를 돌려준다
            auto emit = [&](const IndexKey& key) -> uint32_t {
                if (key.position < 0
                    || static_cast<size_t>(key.position) >= positions.size()) {
                    return UINT32_MAX;
                }

                // 법선이 없으면 면마다 값이 달라지므로 공유하면 안 된다
                const bool shareable = key.normal >= 0;
                if (shareable) {
                    auto found = uniqueVertices.find(key);
                    if (found != uniqueVertices.end()) return found->second;
                }

                Vertex vertex{};
                const vec3& p = positions[static_cast<size_t>(key.position)];
                const vec3& c = colors[static_cast<size_t>(key.position)];
                vertex.position[0] = p.x;
                vertex.position[1] = p.y;
                vertex.position[2] = p.z;
                vertex.color[0] = c.x;
                vertex.color[1] = c.y;
                vertex.color[2] = c.z;

                if (key.normal >= 0
                    && static_cast<size_t>(key.normal) < normals.size()) {
                    const vec3& n = normals[static_cast<size_t>(key.normal)];
                    vertex.normal[0] = n.x;
                    vertex.normal[1] = n.y;
                    vertex.normal[2] = n.z;
                }

                const auto index = static_cast<uint32_t>(result.builder.vertices.size());
                result.builder.vertices.push_back(vertex);
                if (shareable) {
                    uniqueVertices.emplace(key, index);
                } else {
                    pendingFlatNormals.push_back(index);
                }
                return index;
            };

            // 삼각형 팬으로 쪼갠다. 볼록한 면이라면 이걸로 충분하다.
            for (size_t i = 1; i + 1 < corners.size(); ++i) {
                const uint32_t a = emit(corners[0]);
                const uint32_t b = emit(corners[i]);
                const uint32_t c = emit(corners[i + 1]);
                if (a == UINT32_MAX || b == UINT32_MAX || c == UINT32_MAX) {
                    result.error = "face references a vertex that does not exist";
                    return result;
                }
                result.builder.indices.push_back(a);
                result.builder.indices.push_back(b);
                result.builder.indices.push_back(c);
            }
        }
        // vt / mtllib / usemtl / o / g / s 는 지금 쓰지 않으므로 무시한다
    }

    // 법선이 없던 정점들은 자기가 속한 삼각형의 면 법선으로 채운다.
    // (파일에 vn 이 아예 없는 경우 - 이때는 각진 셰이딩이 된다)
    if (!pendingFlatNormals.empty()) {
        auto& verts = result.builder.vertices;
        const auto& idx = result.builder.indices;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            Vertex& v0 = verts[idx[t]];
            Vertex& v1 = verts[idx[t + 1]];
            Vertex& v2 = verts[idx[t + 2]];

            const vec3 p0{v0.position[0], v0.position[1], v0.position[2]};
            const vec3 p1{v1.position[0], v1.position[1], v1.position[2]};
            const vec3 p2{v2.position[0], v2.position[1], v2.position[2]};
            const vec3 n = normalize(cross(p1 - p0, p2 - p0));

            for (Vertex* v : {&v0, &v1, &v2}) {
                if (v->normal[0] == 0.0f && v->normal[1] == 0.0f && v->normal[2] == 0.0f) {
                    v->normal[0] = n.x;
                    v->normal[1] = n.y;
                    v->normal[2] = n.z;
                }
            }
        }
    }

    if (result.builder.vertices.empty() || result.builder.indices.empty()) {
        result.error = "no geometry found";
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace lot_obj
