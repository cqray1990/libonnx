#include <onnx.h>

enum auto_pad_t {
	AUTO_PAD_VALID		= 0,
	AUTO_PAD_NOTSET		= 1,
	AUTO_PAD_SAME_UPPER	= 2,
	AUTO_PAD_SAME_LOWER	= 3,
};

struct operator_pdata_t {
	enum auto_pad_t auto_pad;
	int group;
	int * kernels;
	int nkernel;
	int * dilations;
	int ndilation;
	int * pads;
	int npad;
	int * strides;
	int nstride;

	int cpads[32];
};

static int Conv_init(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat;
	int64_t * ints;
	int i, l;

	if((n->ninput >= 2) && (n->noutput == 1))
	{
		pdat = malloc(sizeof(struct operator_pdata_t));
		if(pdat)
		{
			memset(pdat, 0, sizeof(struct operator_pdata_t));
			switch(shash(onnx_attribute_read_string(n, "auto_pad", "NOTSET")))
			{
			case 0x0e382d15: /* "VALID" */
				pdat->auto_pad = AUTO_PAD_VALID;
				break;
			case 0xc3966fc2: /* "NOTSET" */
				pdat->auto_pad = AUTO_PAD_NOTSET;
				break;
			case 0xcbbc7856: /* "SAME_UPPER" */
				pdat->auto_pad = AUTO_PAD_SAME_UPPER;
				break;
			case 0xcb192d33: /* "SAME_LOWER" */
				pdat->auto_pad = AUTO_PAD_SAME_LOWER;
				break;
			default:
				pdat->auto_pad = AUTO_PAD_NOTSET;
				break;
			}
			pdat->group = onnx_attribute_read_int(n, "group", 1);
			pdat->nkernel = onnx_attribute_read_ints(n, "kernel_shape", &ints);
			if(pdat->nkernel > 0)
			{
				pdat->kernels = malloc(sizeof(int) * pdat->nkernel);
				for(i = 0; i < pdat->nkernel; i++)
					pdat->kernels[i] = ints[i];
			}
			pdat->ndilation = pdat->nkernel;
			pdat->dilations = malloc(sizeof(int) * pdat->ndilation);
			if(pdat->dilations)
			{
				l = onnx_attribute_read_ints(n, "dilations", &ints);
				for(i = 0; i < l; i++)
					pdat->dilations[i] = ints[i];
				for(; i < pdat->ndilation; i++)
					pdat->dilations[i] = 1;
			}
			pdat->npad = pdat->nkernel * 2;
			pdat->pads = malloc(sizeof(int) * pdat->npad);
			if(pdat->pads)
			{
				l = onnx_attribute_read_ints(n, "pads", &ints);
				for(i = 0; i < l; i++)
					pdat->pads[i] = ints[i];
				for(; i < pdat->npad; i++)
					pdat->pads[i] = 0;
			}
			pdat->nstride = pdat->nkernel;
			pdat->strides = malloc(sizeof(int) * pdat->nstride);
			if(pdat->strides)
			{
				l = onnx_attribute_read_ints(n, "strides", &ints);
				for(i = 0; i < l; i++)
					pdat->strides[i] = ints[i];
				for(; i < pdat->nstride; i++)
					pdat->strides[i] = 1;
			}
			n->priv = pdat;
			return 1;
		}
	}
	return 0;
}

static int Conv_exit(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;

	if(pdat)
	{
		if(pdat->kernels)
			free(pdat->kernels);
		if(pdat->dilations)
			free(pdat->dilations);
		if(pdat->pads)
			free(pdat->pads);
		if(pdat->strides)
			free(pdat->strides);
		free(pdat);
	}
	return 1;
}

static int Conv_reshape(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * y = n->outputs[0];
	int ndim = x->ndim;
	int dims[ndim];
	int begin, end;
	int stride, kernel, needed;
	int i;

	memcpy(pdat->cpads, pdat->pads, sizeof(int) * pdat->npad);
    for(i = 0; i < ndim; i++)
    {
    	if(i < 2)
    		dims[i] = x->dims[i];
    	else
    	{
			begin = i - 2;
			end = begin + pdat->nkernel;
			stride = pdat->strides[begin];
			kernel = pdat->kernels[begin];
			switch(pdat->auto_pad)
			{
			case AUTO_PAD_VALID:
				pdat->cpads[begin] = 0;
				pdat->cpads[end] = 0;
				dims[i] = ceilf((x->dims[i] - kernel + 1) / stride);
				break;
			case AUTO_PAD_NOTSET:
				dims[i] = ceilf((x->dims[i] + pdat->cpads[begin] + pdat->cpads[end] - kernel) / stride + 1);
				break;
			case AUTO_PAD_SAME_UPPER:
				needed = ((x->dims[i] + stride - 1) / stride - 1) * stride + kernel - x->dims[i];
				pdat->cpads[begin] = floorf(needed / 2);
				pdat->cpads[end] = needed - pdat->cpads[begin];
				dims[i] = ceilf(x->dims[i] / stride);
				break;
			case AUTO_PAD_SAME_LOWER:
				needed = ((x->dims[i] + stride - 1) / stride - 1) * stride + kernel - x->dims[i];
				pdat->cpads[begin] = floorf((needed + 1) / 2);
				pdat->cpads[end] = needed - pdat->cpads[begin];
				dims[i] = ceilf(x->dims[i] / stride);
				break;
			default:
				break;
			}
    	}
    }
    return onnx_tensor_reshape(y, dims, ndim, x->type);
}

static inline int dim_next(int ndim, int * dims, int * dim_max)
{
	if(ndim == 0)
		return 0;
	while(1)
	{
		ndim = ndim - 1;
		dims[ndim] += 1;
		if(dims[ndim] < dim_max[ndim])
			return 1;
		else
		{
			if(ndim == 0)
				return 0;
			dims[ndim] = 0;
		}
	}
}

static inline int dim_offset(int ndim, int * dims, int * dim_max)
{
	int o, s;
	int i;

	for(i = ndim - 1, o = 0, s = 1; i >= 0; i--)
	{
		o += dims[i] * s;
		s *= dim_max[i];
	}
	return o;
}

static void Conv_float16(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * y = n->outputs[0];
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * w = n->inputs[1];
	struct onnx_tensor_t * b = NULL;
	uint16_t * py = (uint16_t *)y->datas;
	uint16_t * px = (uint16_t *)x->datas;
	uint16_t * pw = (uint16_t *)w->datas;
	uint16_t * pb = NULL;
	float sum, v, weight;
	int ndim = x->ndim;
	int M = w->dims[0];
	int C = w->dims[1];
	int i_dim[ndim];
	int o_dim[ndim];
	int w_dim[ndim];
	int b_dim[ndim];
	int ch, i;

	if(n->ninput > 2)
	{
		b = n->inputs[2];
		pb = (uint16_t *)b->datas;
	}
	memset(o_dim, 0, sizeof(o_dim));
	do {
		b_dim[0] = o_dim[0];
		for(i = 2; i < ndim; i++)
			b_dim[i] = o_dim[i] * pdat->strides[i - 2] - pdat->cpads[i - 2];
		sum = 0;
		memset(w_dim, 0, sizeof(w_dim));
		w_dim[0] = o_dim[1];
		do {
			if(w_dim[1] == 1)
				break;
			i_dim[0] = b_dim[0];
			for(i = 2; i < ndim; i++)
				i_dim[i] = b_dim[i] + w_dim[i] * pdat->dilations[i - 2];
			for(ch = 0; ch < C; ch++)
			{
				i_dim[1] = (o_dim[1] * pdat->group / M) * C + ch;
				w_dim[1] = ch;
				for(i = 0; i < ndim; i++)
				{
					if((i_dim[i] < 0) || (i_dim[i] >= x->dims[i]))
					{
						v = 0;
						break;
					}
				}
				if(i >= ndim)
					v = float16_to_float32(px[dim_offset(ndim, i_dim, x->dims)]);
				for(i = 0; i < ndim; i++)
				{
					if((w_dim[i] < 0) || (w_dim[i] >= w->dims[i]))
					{
						weight = 0;
						break;
					}
				}
				if(i >= ndim)
					weight = float16_to_float32(pw[dim_offset(ndim, w_dim, w->dims)]);
				sum += v * weight;
			}
			w_dim[1] = 0;
		} while(dim_next(ndim, w_dim, w->dims));
		if(pb)
			sum += float16_to_float32(pb[o_dim[1]]);
		py[dim_offset(ndim, o_dim, y->dims)] = float32_to_float16(sum);
	} while(dim_next(ndim, o_dim, y->dims));
}

static void Conv_float32(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * y = n->outputs[0];
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * w = n->inputs[1];
	struct onnx_tensor_t * b = NULL;
	float * py = (float *)y->datas;
	float * px = (float *)x->datas;
	float * pw = (float *)w->datas;
	float * pb = NULL;
	float sum, v, weight;
	int ndim = x->ndim;
	int M = w->dims[0];
	int C = w->dims[1];
	int i_dim[ndim];
	int o_dim[ndim];
	int w_dim[ndim];
	int b_dim[ndim];
	int ch, i;

	if(n->ninput > 2)
	{
		b = n->inputs[2];
		pb = (float *)b->datas;
	}
	memset(o_dim, 0, sizeof(o_dim));
	do {
		b_dim[0] = o_dim[0];
		for(i = 2; i < ndim; i++)
			b_dim[i] = o_dim[i] * pdat->strides[i - 2] - pdat->cpads[i - 2];
		sum = 0;
		memset(w_dim, 0, sizeof(w_dim));
		w_dim[0] = o_dim[1];
		do {
			if(w_dim[1] == 1)
				break;
			i_dim[0] = b_dim[0];
			for(i = 2; i < ndim; i++)
				i_dim[i] = b_dim[i] + w_dim[i] * pdat->dilations[i - 2];
			for(ch = 0; ch < C; ch++)
			{
				i_dim[1] = (o_dim[1] * pdat->group / M) * C + ch;
				w_dim[1] = ch;
				for(i = 0; i < ndim; i++)
				{
					if((i_dim[i] < 0) || (i_dim[i] >= x->dims[i]))
					{
						v = 0;
						break;
					}
				}
				if(i >= ndim)
					v = px[dim_offset(ndim, i_dim, x->dims)];
				for(i = 0; i < ndim; i++)
				{
					if((w_dim[i] < 0) || (w_dim[i] >= w->dims[i]))
					{
						weight = 0;
						break;
					}
				}
				if(i >= ndim)
					weight = pw[dim_offset(ndim, w_dim, w->dims)];
				sum += v * weight;
			}
			w_dim[1] = 0;
		} while(dim_next(ndim, w_dim, w->dims));
		if(pb)
			sum += pb[o_dim[1]];
		py[dim_offset(ndim, o_dim, y->dims)] = sum;
	} while(dim_next(ndim, o_dim, y->dims));
}

static void Conv_float64(struct onnx_node_t * n)
{
	struct operator_pdata_t * pdat = (struct operator_pdata_t *)n->priv;
	struct onnx_tensor_t * y = n->outputs[0];
	struct onnx_tensor_t * x = n->inputs[0];
	struct onnx_tensor_t * w = n->inputs[1];
	struct onnx_tensor_t * b = NULL;
	double * py = (double *)y->datas;
	double * px = (double *)x->datas;
	double * pw = (double *)w->datas;
	double * pb = NULL;
	double sum, v, weight;
	int ndim = x->ndim;
	int M = w->dims[0];
	int C = w->dims[1];
	int i_dim[ndim];
	int o_dim[ndim];
	int w_dim[ndim];
	int b_dim[ndim];
	int ch, i;

	if(n->ninput > 2)
	{
		b = n->inputs[2];
		pb = (double *)b->datas;
	}
	memset(o_dim, 0, sizeof(o_dim));
	do {
		b_dim[0] = o_dim[0];
		for(i = 2; i < ndim; i++)
			b_dim[i] = o_dim[i] * pdat->strides[i - 2] - pdat->cpads[i - 2];
		sum = 0;
		memset(w_dim, 0, sizeof(w_dim));
		w_dim[0] = o_dim[1];
		do {
			if(w_dim[1] == 1)
				break;
			i_dim[0] = b_dim[0];
			for(i = 2; i < ndim; i++)
				i_dim[i] = b_dim[i] + w_dim[i] * pdat->dilations[i - 2];
			for(ch = 0; ch < C; ch++)
			{
				i_dim[1] = (o_dim[1] * pdat->group / M) * C + ch;
				w_dim[1] = ch;
				for(i = 0; i < ndim; i++)
				{
					if((i_dim[i] < 0) || (i_dim[i] >= x->dims[i]))
					{
						v = 0;
						break;
					}
				}
				if(i >= ndim)
					v = px[dim_offset(ndim, i_dim, x->dims)];
				for(i = 0; i < ndim; i++)
				{
					if((w_dim[i] < 0) || (w_dim[i] >= w->dims[i]))
					{
						weight = 0;
						break;
					}
				}
				if(i >= ndim)
					weight = pw[dim_offset(ndim, w_dim, w->dims)];
				sum += v * weight;
			}
			w_dim[1] = 0;
		} while(dim_next(ndim, w_dim, w->dims));
		if(pb)
			sum += pb[o_dim[1]];
		py[dim_offset(ndim, o_dim, y->dims)] = sum;
	} while(dim_next(ndim, o_dim, y->dims));
}

void resolver_default_op_Conv(struct onnx_node_t * n)
{
	if(n->opset >= 11)
	{
		switch(n->inputs[0]->type)
		{
		case ONNX_TENSOR_TYPE_FLOAT16:
			n->init = Conv_init;
			n->exit = Conv_exit;
			n->reshape = Conv_reshape;
			n->operator = Conv_float16;
			break;
		case ONNX_TENSOR_TYPE_FLOAT32:
			n->init = Conv_init;
			n->exit = Conv_exit;
			n->reshape = Conv_reshape;
			n->operator = Conv_float32;
			break;
		case ONNX_TENSOR_TYPE_FLOAT64:
			n->init = Conv_init;
			n->exit = Conv_exit;
			n->reshape = Conv_reshape;
			n->operator = Conv_float64;
			break;
		default:
			break;
		}
	}
	else if(n->opset >= 1)
	{
		switch(n->inputs[0]->type)
		{
		case ONNX_TENSOR_TYPE_FLOAT16:
			n->init = Conv_init;
			n->exit = Conv_exit;
			n->reshape = Conv_reshape;
			n->operator = Conv_float16;
			break;
		case ONNX_TENSOR_TYPE_FLOAT32:
			n->init = Conv_init;
			n->exit = Conv_exit;
			n->reshape = Conv_reshape;
			n->operator = Conv_float32;
			break;
		case ONNX_TENSOR_TYPE_FLOAT64:
			n->init = Conv_init;
			n->exit = Conv_exit;
			n->reshape = Conv_reshape;
			n->operator = Conv_float64;
			break;
		default:
			break;
		}
	}
}
