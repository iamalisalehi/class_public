import multiprocessing
import functools

from .predictor_cache import PredictorCache

from .data_providers import CLASSDataProvider
# from plotting.plot_source_function import plot_source_function
# import matplotlib.pyplot as plt
import numpy as np
import h5py as h5
import os
from time import time, perf_counter
import scipy.interpolate

import torch

from .models import ALL_NETWORK_CLASSES
from . import current_transformer

class BasePredictor:
    def __init__(self, cosmo, input_transformer, target_transformer, k):
        self.cosmo = cosmo

        self.k = k

        self.input_transformer = input_transformer
        self.target_transformer = target_transformer

        self.reset_cache()
        self.reset_times()

    def reset_cache(self):
        self.raw_cache = {}
        self.transformed_cache = {}

    def reset_times(self):
        self.time_prediction = 0
        self.time_input_transformation = 0
        self.time_output_transformation = 0

        self.time_prediction_per_network = {}

    def get_inputs(self, cosmo, tau, selection, mask=None, cached=True):
        """
        This method will be called by `predict_many` to obtain the necessary
        inputs for the NN evaluation from the instance `cosmo` of CLASS
        for the given sampling of `tau`.
        This will return a dictionary of 'raw inputs' (i.e. having physically meaningful values)
        and a dictionary of 'transformed inputs' (i.e. the normalized version used for NN evaluation).
        """
        raise DeprecationWarning("get_inputs is deprecated!")

        # if self.raw_inputs and cached:
        #     assert self.transformed_inputs
        #     all_cached = all(item in self.raw_inputs for item in selection)
        #     all_cached = all_cached and all(item in self.transformed_inputs for item in selection)
        #     if all_cached:
        #         # make sure that we know the inputs at all requested tau values
        #         # by checking that all tau values are in self.raw_inputs["tau"]
        #         all_tau = np.sum(self.raw_inputs["tau"] == tau) == len(tau)
        #         assert all_tau
        #         tau_subset = self.raw_inputs["tau"] == tau
        #         raw_inputs_subset = {key: value[tau_subset] for key, value in self.raw_inputs}
        #         tf_inputs_subset = {key: value[tau_subset] for key, value in self.tf_inputs}
        #         # return self.raw_inputs, self.transformed_inputs
        #         return raw_inputs_subset, tf_inputs_subset

        self.create_provider_if_not_exists(cosmo)
        raw_inputs = self.provider.get_inputs(k=self.k, tau=tau, input_selection=selection)
        assert all(item in raw_inputs for item in selection)
        transformed_inputs = self.input_transformer.transform_inputs(raw_inputs)
        assert all(item in transformed_inputs for item in selection)

        return raw_inputs, transformed_inputs

    def get_limit(self, quantity, raw_inputs):
        """
        This method returns the k -> 0 limit of the source function named `quantity`.
        """
        if quantity in ("t0", "t0_reco", "t0_sw", "t0_reco_no_isw", "t0_reio", "t0_reio_no_isw"):
            return raw_inputs["g"] / 3
        elif quantity == "phi_plus_psi":
            return 4. / 3.
        elif quantity == "delta_m":
            return -6. / 5.
        else:
            return 0

    def predict(self, quantity, tau, provider=None, cache=None):
        """
        Get the network prediction for the given `quantity` and `tau` array.
        The result will be sampled on the `k` array obtained from `self.get_k()` and
        thus have the shape `(len(self.get_k()), len(tau))`
        """
        # TODO re-add the code that you moron deleted
        result, raw_inputs = self._predict(quantity, tau, provider, cache)

        # Here, we should check that in our sampling, the lowest k value
        # is not below cosmo.k_min(), otherwise we might give physically meaningless
        # data to CLASS
        k = self.k
        cosmo = self.cosmo
        k_min_class = cosmo.k_min()

        # result has shape (len(k), len(tau))

        if k_min_class > k[0]:
            k_idx = np.searchsorted(k, k_min_class, side="right")
            assert k_idx > 0
            assert k_min_class < k[k_idx]
            assert k_min_class >= k[k_idx - 1]

            x = (k_min_class - k[k_idx - 1]) / (k[k_idx] - k[k_idx - 1])

            # qty to perform interpolation on
            left = result[k_idx - 1]
            right = result[k_idx]
            if quantity == "delta_m":
                print("delta_m; log interp")
                left = np.log(-left)
                right = np.log(-right)

            interpolated = x * left + (1 - x) * right

            if quantity == "delta_m":
                interpolated = -np.exp(interpolated)

            result = np.insert(result[k_idx:], 0, interpolated, 0)

            # TODO remove assertion in final version
            assert result.shape[0] == len(self.get_k())

            # k_spline = np.array([k[k_idx -1], k[k_idx]])
            # spline = scipy.interpolate.RectBivariateSpline(k_spline, tau, result[k_idx - 1:k_idx + 1], kx=1, ky=1)
            # interpolated = spline(np.array([k_min_class]), tau)
            # # add interpolated row (for k=k_min_class) at beginning
            # # of result array along k axis (0)


            # TODO is this okay for delta_m?

            # import ipdb; ipdb.set_trace()
            # # Fit y = a * exp(b * k) between the two points
            # # surrounding k_min_class
            # b = np.log(result[k_idx] / result[k_idx - 1]) / (k[k_idx] - k[k_idx - 1])
            # a = result[k_idx] / np.exp(b * k[k_idx])
            # import ipdb; ipdb.set_trace()
            # # Get the value of `result` at `k_min_class`
            # result_at_k_min = a * np.exp(b * k_min_class)
            # result = np.insert(result[k_idx:], 0, result_at_k_min, 0)

        # if add_k0:
            # raw_inputs_only_g = cache.get_raw_inputs(["g"])
            # TODO for some physics-y reason, this doesn't work properly?
            # limit = self.get_limit(quantity, raw_inputs_only_g)
            # instead, just do nearest neighbor extrapolation at the lowest k
            # limit = result[0]
            # result = np.insert(result, 0, limit, axis=0)

        return result

    def _predict(self, quantity, tau):
        """
        Predict source function for given quantity.
        Will be implemented by child classes.
        """
        raise NotImplementedError

    def _all_network_input_names(self):
        """
        Return list of all network inputs required.
        Will be implemented by child classes.
        """
        raise NotImplementedError

    def get_k(self):
        # return self.k
        # TODO ??????
        k = self.k
        k_min_class = self.cosmo.k_min()

        if k_min_class > k[0]:
            k_idx = np.searchsorted(k, k_min_class)
            assert k_idx > 0
            assert k_min_class <= k[k_idx]
            assert k_min_class > k[k_idx - 1]
            k_new = np.insert(k[k_idx:], 0, k_min_class)
            return k_new
        else:
            return self.k

    def predict_many(self, quantities, tau):
        """
        Predict the source functions whose names are given as the list `quantities`.
        This will return a numpy array of shape (len(quantities), len(k) + 1, len(tau)).
        The 2nd size is len(k) + 1 instead of len(k) because this function adds another
        row to the S array corresponding to k -> 0.
        This is needed so that CLASS does not have to extrapolate for low k.
        """
        # TODO use self.provider?
        provider = CLASSDataProvider(self.cosmo)

        start = perf_counter()
        # Get ALL inputs (since we want to be doing this only _once_)
        raw_inputs = provider.get_inputs(k=self.k, tau=tau, input_selection=self._all_network_input_names())
        transformed_inputs = self.input_transformer.transform_inputs(raw_inputs)

        # Construct a cache object simplifying the access
        cache = PredictorCache(raw_inputs, transformed_inputs)

        self.time_input_transformation += perf_counter() - start

        predictions = {qty: self.predict(qty, tau, provider, cache=cache) for qty in quantities}

        k = self.get_k()
        k_len = len(k)
        result = np.zeros((len(quantities), len(k), len(tau)))

        # Store predictions in array
        for i, quantity in enumerate(quantities):
            S = predictions[quantity]
            result[i, :, :] = S

        return k, result

    def predict_all(self, tau):
        return self.predict_many(["t0", "t1", "t2", "phi_plus_psi", "delta_m"], tau)

    def untransform(self, quantity, value, raw_inputs):
        start = time()
        result = self.target_transformer.untransform_target(quantity, value, inputs=raw_inputs)
        elapsed = time() - start
        self.time_output_transformation += elapsed

        return result


class ModelWrapper:

    def __init__(self, model):
        self.model = model

    def required_inputs(self):
        raise NotImplementedError

    def __call__(self, inputs):
        pass


class TorchModel(ModelWrapper):

    def __init__(self, model, device, slicing=None):
        self.model = model
        self.device = device
        self.slicing = slicing

    def required_inputs(self):
        return self.model.required_inputs()

    def __call__(self, inputs):
        # self.model.eval()
        from time import perf_counter
        with torch.no_grad():
            a = perf_counter()
            converted_inputs = self._convert_inputs(inputs)
            b = perf_counter()
            S_t = self.model(converted_inputs)
            c = perf_counter()

            t_convert = b - a
            t_infer = c - b
            t_tot = c - a

            # print("convert:\t{}s\ninfer:\t{}s\ntotal:\t{}s".format(
            #     t_convert,
            #     t_infer,
            #     t_tot
            #     ))

        return S_t.cpu().numpy()

    def _convert_inputs(self, inputs):
        # ret = {k: v if v.ndim == 0 else torch.from_numpy(v).float().to(self.device) for k, v in inputs.items() if not isinstance(v, tuple)}
        ret = {k: v if v.ndim == 0 else torch.from_numpy(v.astype(np.float32)).to(self.device) for k, v in inputs.items() if not isinstance(v, tuple)}
        return ret


class TreePredictor(BasePredictor):

    def __init__(self, cosmo, input_transformer, target_transformer, models, rules, funcs=None, k=None):
        super().__init__(cosmo, input_transformer, target_transformer, k=k)

        self.models = models
        self.rules = rules
        self.funcs = funcs if funcs is not None else {}
        self.cache = {}
        self.verbose = False

    def log(self, *args, **kwargs):
        if self.verbose:
            print(*args, **kwargs)

    def _all_network_input_names(self):
        from itertools import chain
        return set(chain(*(mod.required_inputs() for mod in self.models.values())))

    def _predict(self, quantity, tau, provider, cache):
        cosmo = self.cosmo

        if quantity in self.models:
            S, raw_inputs = self._predict_from_model(quantity, cosmo, tau, provider, cache)
        else:
            S, raw_inputs = self._predict_from_combine(quantity, cosmo, tau, provider, cache)

        if cosmo.nn_cheat_enabled() and quantity in cosmo.nn_cheat_sources():
            # Here, the `quantity` should not be predicted by a network, but
            # instead be taken from CLASS.
            # First, emit a warning message to make sure that this is not
            # accidentally enabled
            print("WARNING: 'CHEATING' IS ENABLED FOR QUANTITY '{}'".format(quantity))
            # It is guaranteed that if `cosmo.nn_cheat_mode()` is true,
            # the perturbation module has been fully executed and we can
            # thus simply take the source function in question from there.
            # This is done after evaluating a networks, because we also need
            # to return the raw inputs, i.e. we only replace the source function.
            S_cheat, k_cheat, tau_cheat = cosmo.get_sources()
            # it remains to perform interpolation of the source function onto
            # the desired (k, tau)-grid.
            spline = scipy.interpolate.RectBivariateSpline(k_cheat, tau_cheat, S_cheat[quantity])
            # this function must also return the `raw_inputs` dict
            return spline(self.k, tau), raw_inputs

        if quantity in self.funcs:
            self.funcs[quantity](S, raw_inputs)

        return S, raw_inputs

    def _predict_from_model(self, quantity, cosmo, tau, provider, cache):
        assert quantity not in self.rules
        if quantity in self.cache:
            return self.cache[quantity]

        model = self.models[quantity]

        in_select = model.required_inputs()

        self.log("Evaluating model for", quantity)

        # Check whether we should evaluate model only at certain tau
        slicing = model.slicing
        if slicing is not None:
            mask = slicing.which(cosmo, tau)
            tau_eval = tau[mask]
        else:
            tau_eval = tau
            mask = None

        start_input_retrieval = time()
        raw_inputs = cache.get_raw_inputs(in_select, tau_mask=mask)
        inputs = cache.get_transformed_inputs(in_select, tau_mask=mask)
        elapsed = time() - start_input_retrieval
        self.time_input_transformation += elapsed

        start_predict = perf_counter()
        S_t = model(inputs)
        elapsed = perf_counter() - start_predict

        result_shape = list(S_t.shape)
        result_shape[0] = tau.size
        result = np.zeros(result_shape)

        if slicing is not None:
            result[mask] = S_t
            self.log("Slicing output for quantity", quantity)
        else:
            result[:, :] = S_t

        S_t = result

        self.time_prediction_per_network[quantity] = elapsed
        self.time_prediction += elapsed

        # Swap k and tau axis
        S_t = np.swapaxes(S_t, 0, 1)
        if isinstance(quantity, tuple) or isinstance(quantity, list):
            # If quantity is a 'container' for multiple quantities (e.g. phi+psi and delta_m),
            # transform the components individually
            S = np.stack([self.untransform(q, S_t[..., i], raw_inputs) for i, q in enumerate(quantity)], axis=2)
        else:
            S = self.untransform(quantity, S_t, raw_inputs)

        self.cache[quantity] = (S, raw_inputs)
        return S, raw_inputs

    def _predict_from_combine(self, quantity, cosmo, tau, provider, cache):
        assert quantity not in self.models
        parents, combine, pass_cosmo = self.rules[quantity]

        contributions = []
        raw_inputs = {}
        self.log("Computing {} as combination of {}.".format(quantity, parents))
        for parent in parents:
            contrib, raw_inp = self._predict(parent, tau, provider, cache)
            contributions.append(contrib)
            raw_inputs.update(raw_inp)

        if pass_cosmo:
            S = combine(contributions, cosmo, tau)
        else:
            S = combine(contributions)

        return S, raw_inputs


def build_predictor(cosmo, device_name="cpu"):
    workspace = cosmo.nn_workspace()
    device = torch.device(device_name)

    k  = workspace.loader().k()
    kt = torch.from_numpy(k).float().to(device)

    models, rules = load_models(workspace, ALL_NETWORK_CLASSES, kt, device)
    input_transformer, target_transformer = current_transformer.get_pair(workspace.normalization_file, k)

    predictor = TreePredictor(
        cosmo,
        input_transformer, target_transformer,
        models, rules,
        k=k,
    )

    return predictor

def load_models(workspace, classes, k, device):
    models = [ctor(k) for ctor in classes]
    for model in models:
        state_dict = torch.load(workspace.model_path(model.name()), map_location=device)
        model.load_state_dict(state_dict)
        model.to(device)
        model.eval()

    def model_key(model):
        targets = model.source_functions()
        if len(targets) == 1:
            return targets[0]
        else:
            return tuple(targets)

    def model_wrapper(model):
        return TorchModel(model, device, slicing=model.slicing())

    model_dict = {model_key(m): model_wrapper(m) for m in models}

    rules = {
            "t0":           (("t0_reco_no_isw", "t0_reio_no_isw", "t0_isw"), sum, False),
            "t2":           (("t2_reco", "t2_reio"), sum, False),
            "phi_plus_psi": ((("phi_plus_psi", "delta_m"),), Channel(0), False),
            "delta_m":      ((("phi_plus_psi", "delta_m"),), Channel(1), False),
            }

    return model_dict, rules


class PredictionDescriptor:
    def __init__(self, model_dict, rules):
        """
        Represents how the models need to be evaluated.

        `model_dict` will be a dict from source functions names to `TorchModel`
        instances.

        `rules` will be a dict describing how source functions that are not
        implemented as single networks will be 'assembled' from the outputs
        of other networks.
        For that, `rules` maps the names of the source functions to 3-tuples
        of the form `(tuple_of_dependencies, function_to_apply_to_dependencies, unused)`.
        """
        self.model_dict = model_dict
        self.rules = rules


class Channel:
    def __init__(self, n):
        self.n = n

    def __call__(self, items):
        return items[0][..., self.n]
