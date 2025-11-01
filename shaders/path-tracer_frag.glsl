#version 430

#define NO_INTERSECTION intersection(0, material(0, 0, 0, vec3(0), vec3(0), vec3(0)), vec3(0), vec3(0), false)
#define MAX_BOUNCE 128
#define EPSILON 0.01
#define PI 3.14159265358979323846
#define TWOPI 6.28318530717958647692

//layout(std140) uniform Material // Must match the GPUMaterial defined in src/mesh.h
//{
    //vec3 kd;
	//vec3 ks;
	//float shininess;
	//float transparency;
//};

uniform sampler2D colorMap;
uniform bool hasTexCoords;
uniform bool useMaterial;

uniform int accum_sample;

uniform samplerCube skybox;

uniform int num_triangles;
uniform int num_bvh_nodes;

uniform sampler2D accum;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

in vec2 uv;

struct vertex {
	vec3 position;
	vec3 normal;
	vec2 uv;
};

struct material {
	float specular;
	float diffuse;
	float refract;
	vec3 kd;
	vec3 ks;
	vec3 ke;

	sampler2D kd_tex;
};

struct camera {
	vec3 pos;

	/* Screen plane positions */
	vec3 p0;
	vec3 p1;
	vec3 p2;

	bool is_moving;
};

struct intersection {
	float distance;
	material mat;
	vec3 normal;
	vec3 point;

	bool hits;
};

struct scene {
	vec2 screen;
	bool debug;
	int stochastic_samples;
	camera cam;
};

struct triangle {
	vertex v0;
	vertex v1;
	vertex v2;

	material mat;
};

struct bvh_node {
	vec3 min;
	vec3 max;
	int left; // index of left child node
	int right;
	int first; // index of first triangle in the TrianglesBuffer
	int count; // number of triangles in this leaf
};

uint seed;

layout (std140) uniform Triangles {
	triangle tris[0xffff/208];
};

layout (std140) uniform BVH {
	bvh_node bvh_nodes[0xffff/208/4];
};

layout (std140) uniform ubscene {
	scene sc;
};

layout(location = 0) out vec4 fragColor;

float rand(inout uint seed) {
    seed = 1664525u * seed + 1013904223u;
    seed += 1664525u * seed;
    seed ^= (seed >> 16u);
    seed += 1664525u * seed;
    seed ^= (seed >> 16u);
    return seed * pow(0.5, 32.0);
}

vec3 uniform_hemisphere(float u1, float u2) {
	float z = 1 - 2*u1;
	float r = sqrt(max(0, 1 - z*z));
	float phi = TWOPI * u2;
	float x = r * cos(phi);
	float y = r * sin(phi);

	return vec3(x, y, z);
}
//https://github.com/Jojendersie/gpugi/blob/5d18526c864bbf09baca02bfab6bcec97b7e1210/gpugi/shader/intersectiontests.glsl#L63
intersection intersect(triangle t, vec3 E, vec3 D, float mint, float maxt) {
	intersection s = NO_INTERSECTION;

	vec3 p0 = t.v0.position;
	vec3 p1 = t.v1.position;
	vec3 p2 = t.v2.position;

	vec3 e0 = p1 - p0;
	vec3 e1 = p0 - p2;

	vec3 tri_norm = cross(e1, e0);

	vec3 e2 = (1.0 / dot(tri_norm, D)) * (p0 - E);
	vec3 i = cross(D, e2);

	vec3 bary;
	bary.y = dot(i, e1);
	bary.z = dot(i, e0);
	bary.x = 1.0 - bary.y - bary.z;

	float dist = dot(tri_norm, e2);

	if(dist >= mint && dist <= maxt && all(greaterThanEqual(bary, vec3(0.0)))) {
		s.distance = dist;
		s.hits = true;
		s.mat = t.mat;
		s.normal = normalize(tri_norm); //TODO interpolate normal from bary coords
		//s.normal = normalize(bary.x * t.v0.normal + bary.y * t.v1.normal + bary.z * t.v2.normal);
		s.point = E + s.distance * D;
	}

	return s;
}

/* TODO rewrite */
bool intersectAABB(vec3 bmin, vec3 bmax, vec3 E, vec3 D, float mint, float maxt) {
	float tmin = mint;
	float tmax = maxt;

	for (int i = 0; i < 3; ++i) {
		float invD = 1.0 / D[i];
		float t0 = (bmin[i] - E[i]) * invD;
		float t1 = (bmax[i] - E[i]) * invD;
		if (invD < 0.0) {
			// swap
			float tmp = t0; t0 = t1; t1 = tmp;
		}
		tmin = max(tmin, t0);
		tmax = min(tmax, t1);
		if (tmax < tmin) return false;
	}
	return true;
}

/* TODO rewrite */
intersection intersect(vec3 E, vec3 D, float mint, float maxt) {
	intersection s = NO_INTERSECTION;

	const int STACK_SIZE = 128;
	int stack[STACK_SIZE];
	int sp = 0;
	stack[sp++] = 0; // push root node

	float currentClosest = maxt;

	while (sp > 0) {
		int node_index = stack[--sp]; // pop
		bvh_node current_node = bvh_nodes[node_index];

		if (!intersectAABB(current_node.min, current_node.max, E, D, mint, min(currentClosest, maxt))) {
			continue;
		}

		if (current_node.count > 0) {
			// Leaf node: test triangles
			int first = current_node.first;
			int last = first + current_node.count;

			for (int i = first; i < last; i++) {
				intersection t = intersect(tris[i], E, D, mint, maxt);
				if (t.hits) {
					if (!s.hits || (t.distance < s.distance && t.distance > EPSILON)) {
						s = t;
						currentClosest = t.distance;
					}
				}
			}
		} else {
			// Push nodes

			int left = current_node.left;
			int right = current_node.right;

			if (right >= 0) {
				if (sp < STACK_SIZE) stack[sp++] = right;
			}

			if (left >= 0) {
				if (sp < STACK_SIZE) stack[sp++] = left;
			}
		}
	}

	return s;
}

vec3 specular_step(inout intersection s, inout vec3 origin, inout vec3 spec) {
	vec3 L = normalize(s.point - origin);
	vec3 E = origin;

	vec3 diffuse = vec3(0);
	vec3 col = vec3(0);

	vec3 R = reflect(L, normalize(s.normal));


	//vec3 diffusedir = normalize(vec3(rand(seed) - 0.5, rand(seed) - 0.5, rand(seed) - 0.5));
	vec3 diffusedir = normalize(uniform_hemisphere(rand(seed), rand(seed)));

	if(dot(diffusedir, s.normal) < 0) {
		diffusedir = -diffusedir;
	}

	float focus = dot(s.normal, diffusedir);
	spec *= s.mat.kd * focus;

	bool specular_bounce = rand(seed) <= 1 - s.mat.diffuse;

	if(specular_bounce) {
		R = mix(diffusedir, R, s.mat.specular);
	} else {
		R = diffusedir;
	}

	E = s.point;
	intersection t = intersect(E, R, EPSILON, 1e9);
	if(!t.hits) {
		col += texture(skybox, R).rgb * spec;
	}

	s = t;
	origin = E;

	return col;
}

float fresnel(float n1, float n2, vec3 I, vec3 N) {
	float R0 = (n1 - n2) / (n1 + n2); R0 *= R0;
	float costheta = -dot(I, N);
	float x = 1 - costheta;

	float R = R0 + (1-R0)*x*x*x*x*x;

	return R;
}

/* Compute a single refraction bounce/step */
vec3 refraction_step(inout intersection s, inout vec3 origin, inout float refl_mult, int max_depth) {
	vec3 col = vec3(0);

	intersection st, sm;

	float base_mult = refl_mult;

	vec3 D = normalize(s.point - origin);
	vec3 N = normalize(s.normal);

	bool inside = dot(D, N) > 0;

	if(inside) {
		N = -N;
	}

	float n1 = 1;
	float n2 = s.mat.refract;

	float refl;

	if(inside) {
		refl = fresnel(n2, n1, D, N);
	} else {
		refl = fresnel(n1, n2, D, N);
	}

	vec3 R;
	vec3 Rf = refract(D, N, inside ? s.mat.refract : 1 / s.mat.refract);
	vec3 Rm = reflect(D, N);

	/* transmitted */
	st = intersect(s.point, Rf, EPSILON, 1e9);

	/* reflected */
	sm = intersect(s.point, Rm, EPSILON, 1e9);

	bool refl_bounce = rand(seed) <= refl;


	origin = s.point;
	refl_mult *= (1 - refl);

	if(refl_bounce) {
		R = Rm;
		s = sm;
	} else {
		R = Rf;
		s = st;
	}

	if(!s.hits) {
		col = texture(skybox, R).rgb;
	}

	return col;
}

vec3 render() {
	vec3 col = vec3(0, 0, 0);
	vec2 pixel = gl_FragCoord.xy;

	float r1 = rand(seed) - 0.5;
	float r2 = rand(seed) - 0.5;

	vec2 jitter = vec2(2*r1, 2*r2);

	//jitter /= pixel * 0.5;

	vec3 E = sc.cam.pos;

	vec3 p0 = sc.cam.p0;
	vec3 p1 = sc.cam.p1;
	vec3 p2 = sc.cam.p2;

	vec3 u = (p1 - p0);
	vec3 v = (p2 - p0);

	pixel += jitter;

	float a = pixel.x / sc.screen.x;
	float b = pixel.y / sc.screen.y;

	vec3 p = p0 + a*u + b*v;

	vec3 D = normalize(p - E);

	intersection s = intersect(E, D, EPSILON, 1e9);

	if(!s.hits) {
		return vec3(texture(skybox, D).rgb);
	}

	float reflect_mult = 1;
	vec3 mask = vec3(1);
	for(int bounce = 0; bounce < MAX_BOUNCE; bounce++) {
		if(!s.hits) {
			break;
		}

		col += s.mat.ke * mask;

		if(s.mat.refract > 0) {
			col += refraction_step(s, E, reflect_mult, 2) * mask;
		} else {
			col += specular_step(s, E, mask);
		}
	}

	//col = clamp(col, vec3(0, 0, 0), vec3(1, 1, 1));

	/* Color of the pixel to return */
	return col;
}

void main()
{
	vec2 coord = gl_FragCoord.xy;

	uint idx = uint(coord.x + coord.y * sc.screen.x);
	seed = idx * uint(accum_sample);

	vec3 accumCol = texture(accum, uv).xyz;

	/* clear the accumulated color if we're moving the camera */
	if(sc.cam.is_moving) {
		accumCol = vec3(0);
	}

	fragColor = vec4(render() + accumCol, 1);
}
