#include <fmt/format.h>

#include "scene.h"
#include "mesh.h"

void Scene::addMesh(std::filesystem::path filePath, bool normalize) {
    if (!std::filesystem::exists(filePath))
        throw MeshLoadingException(fmt::format("File {} does not exist", filePath.string().c_str()));

	std::vector<Mesh> subMeshes = loadMesh(filePath, { .normalizeVertexPositions = normalize });

	for (auto& mesh : subMeshes) {
		this->meshes.emplace_back(std::move(mesh));
		this->meshTransforms.emplace_back(glm::mat4(1.0f));

		for(auto& triangle : mesh.triangles) {
			Triangle tri;
			tri.v0 = mesh.vertices[triangle.x];
			tri.v1 = mesh.vertices[triangle.y];
			tri.v2 = mesh.vertices[triangle.z];
			tri.mat = mesh.material;
		}
	}
}

int Scene::buildBVH(int start = 0, int end = this->triangles.size(), int maxTris = 4) {
	std::span<Triangle> triangleSpan(this->triangles.data() + start, end - start);

	glm::vec3 minBounds( std::numeric_limits<float>::max());
	glm::vec3 maxBounds(-std::numeric_limits<float>::max());
	for (const auto& tri : triangleSpan) {
		minBounds = 
			glm::min(minBounds, glm::min(tri.v0.position, glm::min(tri.v1.position, tri.v2.position)));
		maxBounds = 
			glm::max(maxBounds, glm::max(tri.v0.position, glm::max(tri.v1.position, tri.v2.position)));
	}

	BVHNode node;

	node.min = minBounds;
	node.max = maxBounds;

	if (end - start <= maxTris) {
		node.left = -1;
		node.right = -1;
		node.first = start;
		node.count = end - start;

		this->bvh.push_back(node);

		return this->bvh.size() - 1;
	}

	/* TODO (optimisation): order the vertices in such a way that the bounding box overlap is minimal */
	int mid = start + (end - start) / 2;

	this->bvh.push_back(node);
	int node_idx = this->bvh.size() - 1;

	int leftChild = buildBVH(start, mid, maxTris);
	int rightChild = buildBVH(mid, end, maxTris);

	this->bvh[node_idx].left = leftChild;
	this->bvh[node_idx].right = rightChild;

	return node_idx;
}
