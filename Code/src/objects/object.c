#include "object.h"
#include "ray.h"
#include "intersection.h"
#include "vector.h"
#include "utils.h"
#include "light.h"
#include "photon.h"
#include <math.h>
#include "sampler.h"
#include "maths.h"
#include <string.h>

void object_calculate_reflected_colour( object_t *object, scene_t *scene, intersection_t *info)
{
	ray_t *ray = &info->incident;
	if(ray->depth)
	{
		double surface_normal[3];
		intersection_t temp;
		ray_t new_ray;
		new_ray.depth = ray->depth - 1;
		vector_copy(info->point, new_ray.origin);
		maths_calculate_reflected_ray(ray->normal, info->normal, new_ray.normal);

		if(intersection_ray_scene(&new_ray, scene, &temp))
		{
			vector_copy(temp.scene.colour, info->scene.colour);
		}
		else {
			memset(info->scene.colour, 0x00, sizeof(double) * 3);
		}
	}
}



void object_calculate_refracted_colour( object_t *object, scene_t *scene, intersection_t *info)
{
	ray_t  refracted_ray;
	ray_t *incident = &info->incident;
	if(incident->depth)
	{
		refracted_ray.depth = incident->depth - 1;
		double t = maths_calculate_refracted_ray(incident->normal, info->normal, object->material.ior, refracted_ray.normal);
		if(incident->depth != 10 && incident->depth != 9)
		{
			double e = randf(0, 1.0);
			if(e < t)
			{
				intersection_t refraction_info;
				vector_copy(info->point, refracted_ray.origin);

				if(intersection_ray_scene(&refracted_ray, scene, &refraction_info))
				{
					vector_copy(refraction_info.scene.colour, info->scene.colour);
				}
			}
			else
			{
				object_calculate_reflected_colour(object, scene, info);
			}
		}
		else
		{
			intersection_t refraction_info;
			vector_copy(info->point, refracted_ray.origin);

			intersection_ray_scene(&refracted_ray, scene, &refraction_info);

			object_calculate_reflected_colour(object, scene, info);
			for(int i = 0; i < 3; i++)
			{
				info->scene.colour[i] = (1 - t) * info->scene.colour[i] + t * refraction_info.scene.colour[i];
			}
		}

	}
}

void object_calculate_texture_colour( object_t *object, intersection_t *info, double colour_out[3])
{
	if(object->material.texture)
	{
		double tex[2];
		CALL(object, get_tex, info, tex);
		texture_sample(object->material.texture, tex[0], tex[1], colour_out);
	}
	else
	{
		memcpy(colour_out, object->material.diffuse, sizeof(double) * 3);
	}
}

void calculate_diffuse_direct( object_t *object, scene_t *scene, ray_t *ray,
									  intersection_t *info, double colour_out[3])
{

	light_t **lights = list_data(scene->lights);
	int num_lights   = list_size(scene->lights);

	double radiance[3];
	memset(radiance, 0x00, sizeof(double) * 3);
	for(int i = 0; i < num_lights; i++)
	{
		light_calculate_radiance(lights[i], scene, info->point, info->normal, radiance);
		radiance[0] /= PI;
		radiance[1] /= PI;
		radiance[2] /= PI;
		vector_add(colour_out, radiance, colour_out);
	}
}

void calculate_diffuse_indirect ( object_t *object, scene_t *scene, ray_t *ray,
									  intersection_t *info, double colour_out[3])
{
#if PMAP
	//Simulate the first diffuse bounce of the light to estimate the radiance at the point.
#if FAST_DIFFUSE
	double inten[3] = {0, 0, 0};
	photon_map_estimate_radiance(scene->global, info->point, ray->normal, inten);
	colour_out[0] += inten[0] / PI;
	colour_out[1] += inten[1] / PI;
	colour_out[2] += inten[2] / PI;
#else
#if 1
	double sample_col[3] = {0, 0, 0};
	int num_samples = 10;
	double x[3];

	double basisx[3];
	double basisy[3];
	maths_basis(info->normal, basisy, basisy);

	ray_t diffuse_ray;
	vector_copy(info->point, diffuse_ray.origin);

	for(int i = 0; i < num_samples; i++)
	{
		for(int j = 0; j < num_samples; j++)
		{
			double inten[] = {0, 0, 0};
			diffuse_ray.depth = ray->depth;
			//Sample the BRDF (TODO: Add general BRDF computation)
			sample_hemi_cosine(info->normal, diffuse_ray.normal);

			intersection_t temp;
			if(intersection_photon_scene(&diffuse_ray, scene, &temp))
			{
				double tex[3];
				object_calculate_texture_colour(object, info, tex);
				object_t *new_obj = temp.scene.object;
				photon_map_estimate_radiance(scene->global, temp.point, temp.normal, inten);

				sample_col[0] += (tex[0]) * inten[0] * vector_dot(info->normal, diffuse_ray.normal);
				sample_col[1] += (tex[1]) * inten[1] * vector_dot(info->normal, diffuse_ray.normal);
				sample_col[2] += (tex[2]) * inten[2] * vector_dot(info->normal, diffuse_ray.normal);
			}
		}
	}

	colour_out[0] += sample_col[0] / (num_samples * num_samples);
	colour_out[1] += sample_col[1] / (num_samples * num_samples);
	colour_out[2] += sample_col[2] / (num_samples * num_samples);
#endif
#endif

#endif
}


void calculate_diffuse_caustic( object_t *object, scene_t *scene, ray_t *ray,
									  intersection_t *info, double colour_out[3])
{
#if PMAP
	double inten[] = {0, 0, 0};

	//We are directly sampling the radiance due to caustics.
	photon_map_estimate_radiance(scene->caustic, info->point, info->normal, inten);

	colour_out[0] += inten[0] / PI;
	colour_out[1] += inten[1] / PI;
	colour_out[2] += inten[2] / PI;
#endif
}

void object_calculate_diffuse_colour( object_t *object, scene_t *scene, intersection_t *info)
{
	double tex[3];
	double *colour_out =  info->scene.colour;
	ray_t  *incident   = &info->incident;
	memset(colour_out, 0x00, sizeof(double) * 3);
	object_calculate_texture_colour(object, info, tex);

#if !FAST_DIFFUSE
	calculate_diffuse_direct(object,   scene, incident, info, colour_out);
#endif
	calculate_diffuse_indirect(object, scene, incident, info, colour_out);
	calculate_diffuse_caustic(object,  scene, incident, info, colour_out);

	colour_out[0] *= tex[0];
	colour_out[1] *= tex[1];
	colour_out[2] *= tex[2];
}

static void default_shade_func(object_t *object, scene_t *scene, intersection_t *info)
{
	double eps = randf(0.0, 1.0);
	material_t *mat = &object->material;
	if(eps < mat->av_diff)
	{
		object_calculate_diffuse_colour(object, scene, info);
	}
	else if(eps < mat->av_diff + mat->av_refl)
	{
		object_calculate_reflected_colour(object, scene, info);
	}
	else if(eps < mat->av_diff + mat->av_refl + mat->av_refr)
	{
		object_calculate_refracted_colour(object, scene, info);
	}
}

static int  default_intersection_func(object_t *o, ray_t *ray, intersection_t *info)
{
	ERROR("Object Function Not Implemented.\n");
}

static void default_bounds_func(object_t *o, aabb_t *bounds)
{
	ERROR("Object Function Not Implemented.\n");
}

static void default_normal_func(object_t *o, intersection_t *info, double *normal)
{
	ERROR("Object Function Not Implemented.\n");
}

static void default_tex_func(object_t *o, intersection_t *info, double *tex)
{
	ERROR("Object Function Not Implemented.\n");
}

static void default_print_func(object_t *o)
{
	ERROR("Object Function Not Implemented.\n");
}

static void default_delete_func(object_t *o)
{
	ERROR("Object Function Not Implemented.\n");
}

//Set the default function pointers
void object_init(object_t *o)
{
	o->shade        = default_shade_func;
	o->intersection = default_intersection_func;
	o->get_bounds   = default_bounds_func;
	o->get_normal   = default_normal_func;
	o->get_tex      = default_tex_func;
	o->shade        = default_shade_func;
	o->print        = default_print_func;
	o->delete       = default_delete_func;
}
