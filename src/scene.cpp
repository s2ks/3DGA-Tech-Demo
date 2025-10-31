#include <fmt/format.h>
#include <assert.h>

#include "scene.h"
#include "mesh.h"

Scene::Scene() {
	glGenBuffers(1, &this->triSSBO);
	glGenBuffers(1, &this->bvhSSBO);
}

std::vector<int> Scene::addMesh(std::filesystem::path filePath, bool normalize) {
    if (!std::filesystem::exists(filePath)) {
        throw MeshLoadingException(fmt::format("File {} does not exist", filePath.string().c_str()));
	}

	std::vector<Mesh> subMeshes = loadMesh(filePath, { .normalizeVertexPositions = normalize });
	std::vector<int> meshIndices;

	for (auto& mesh : subMeshes) {
		this->meshes.emplace_back(std::move(mesh));
		this->meshTransforms.emplace_back(glm::mat4(1.0f));
		meshIndices.emplace_back((int) this->meshes.size() - 1);

		for(auto& triangle : mesh.triangles) {
			Triangle tri;
			tri.v0 = mesh.vertices[triangle.x];
			tri.v1 = mesh.vertices[triangle.y];
			tri.v2 = mesh.vertices[triangle.z];
			tri.mat = mesh.material;
		}
	}

	this->updateTriSSBO();

	return meshIndices;
}

size_t Scene::buildBVH(int start, int end, int maxTris = 4) {
	std::span<Triangle> triangleSpan(this->triangles.data() + start, (unsigned) (end - start));

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
		node.count = (end - start);

		this->bvh.push_back(node);

		return this->bvh.size() - 1;
	}

	/* TODO (optimisation): order the vertices in such a way that the bounding box overlap is minimal */
	int mid = (start + (end - start) / 2);

	this->bvh.push_back(node);
	size_t node_idx = this->bvh.size() - 1;

	size_t leftChild = buildBVH(start, mid, maxTris);
	size_t rightChild = buildBVH(mid, end, maxTris);

	this->bvh[node_idx].left = (int) leftChild;
	this->bvh[node_idx].right = (int) rightChild;

	return node_idx;
}

void Scene::buildBVH(int maxTris = 4) {
	this->bvh.clear();
	size_t root = buildBVH(0, (int) this->triangles.size(), maxTris);

	this->updateBVHSSBO();

	assert(root == 0);
}

void Scene::updateTriSSBO() {
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, this->triSSBO);
	glBufferData(
		GL_SHADER_STORAGE_BUFFER,
		(GLsizeiptr) (this->triangles.size() * sizeof(Triangle)),
		this->triangles.data(),
		GL_STATIC_DRAW
	);

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void Scene::updateBVHSSBO() {
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, this->bvhSSBO);
	glBufferData(
		GL_SHADER_STORAGE_BUFFER,
		(GLsizeiptr) (this->bvh.size() * sizeof(BVHNode)),
		this->bvh.data(),
		GL_STATIC_DRAW
	);

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
